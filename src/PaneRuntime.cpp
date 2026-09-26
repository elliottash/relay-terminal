// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pane.h"
#include "PaneChrome.h"   // the remote chip this pane owns (setRemotePersistent, #XQ8F)
#include "AppPaths.h"     // relay::scratchpaths: this pane's session scratch root (#DVV2)
#include <QProcess>        // the master keepalive runs detached
#include <QScrollArea>     // the running line opened to its whole text (#JDN4)

bool Pane::restoreAgentPrompt() {
    if (!m_editor->toPlainText().isEmpty() || inQueueSelection()
        || m_recallPrompt.text.isEmpty() || m_recallPrompt.taken) return false;
    m_recallPrompt.taken = true;
    // The correction is a new prompt, never an answer to the cancelled turn's ask.
    // Withdraw it locally now; the worker's cancellation closes its matching waiter.
    closeQuestion(QStringLiteral("editing the interrupted prompt"));
    clearPrefixMode(true);
    setPrefixMode(QStringLiteral("agent"));
    m_editor->setPlainText(m_recallPrompt.text);
    m_editor->moveCursor(QTextCursor::End);
    m_escTimer.stop();
    changed();
    return true;
}

void Pane::trackRecallEvent(const QString &type, const QJsonObject &event) {
    const QString item = event.value(QStringLiteral("id")).toString();
    if (type == QStringLiteral("queued")) {
        if (!m_recallPrompt.request.isEmpty()
            && event.value(QStringLiteral("request_id")).toString() == m_recallPrompt.request)
            m_recallPrompt.item = item;
    } else if (type == QStringLiteral("agent_started")) {
        if (m_recallPrompt.item != item) {
            // A correction can already be on the wire when the old turn's start arrives.
            // Keep that newer submission; the old lifecycle must not replace its draft.
            if (!m_recallPrompt.request.isEmpty()
                && (m_pendingPrompts.contains(m_recallPrompt.request)
                    || m_itemPrompts.contains(m_recallPrompt.item))) return;
            const PendingPrompt prompt = m_itemPrompts.value(item);
            const QString text = prompt.fix || prompt.handoff ? QString()
                : prompt.text.isEmpty() ? m_workerPrompts.value(item).text : prompt.text;
            m_recallPrompt = {text, QString(), item, false};
        }
    } else if (type == QStringLiteral("tool_started")) {
        // The turn this recall tracks has run a tool call: the world has been touched, so the
        // combined "take the prompt back and stop the turn" stops being offered from here on.
        if (!m_recallPrompt.item.isEmpty()
            && event.value(QStringLiteral("turn_id")).toString() == m_recallPrompt.item)
            m_recallPrompt.toolsRun = true;
    } else if ((type == QStringLiteral("agent_finished") && item == m_recallPrompt.item)
               || (type == QStringLiteral("error") && !m_recallPrompt.request.isEmpty()
                   && item == m_recallPrompt.request)
               || type == QStringLiteral("ready") || type == QStringLiteral("reset")
               || type == QStringLiteral("configured")) {
        m_recallPrompt = {};
    }
}

bool Pane::eventFilter(QObject *object, QEvent *event) {
        // Card #XCXD: the two queue lanes sit side by side on a wide strip and stack on a narrow
        // one. The layout is chosen when the strip is rebuilt, so a resize that crosses the
        // boundary rebuilds it once — the cached mode keeps this from looping.
        if (object == m_queueStrip && event->type() == QEvent::Resize) {
            const bool stackedNow = m_queueStrip->width() < 640;
            if (stackedNow != m_queueLanesStacked && m_terminalQueueList && m_terminalQueueList->isVisible())
                QTimer::singleShot(0, this, [this] {
                    if (m_queueStrip && m_terminalQueueList && m_terminalQueueList->isVisible()
                        && (m_queueStrip->width() < 640) != m_queueLanesStacked && !m_rebuildingQueueStrip)
                        rebuildQueueStrip();
                });
            return false;
        }
        // Answered with the mouse while Y and N were right there: the hint is owed (WARP.md's
        // standing rule). Recorded on the press rather than on `clicked`, because Space on the
        // focused button is `clicked` too and is not the slow path.
        if (event->type() == QEvent::MouseButtonPress && guestBarOwns(object)) m_guestBarMouse = true;
        // A guest's permission question has the keyboard while it is up (GT7X, 26.4). Only the
        // bar's own widgets are filtered, so the terminal keeps every key when it is not.
        if (event->type() == QEvent::KeyPress && guestBarOwns(object)
            && guestBarKey(static_cast<QKeyEvent *>(event)))
            return true;
        // Moving the pane by its header comes first: once a drag is under way it owns the mouse,
        // so the folder line below cannot open an explorer when the drag happens to end on it.
        if (headerDragEvent(object, event)) return true;
        // A toast that is up when the layout changes (the fix loop opening the agent transcript
        // resizes the terminal host) would strand over the composer; re-anchor it like the other
        // floating overlays (placeQueueStrip and friends in resizeEvent).
        if (object == m_terminalHost && event->type() == QEvent::Resize && m_toast && m_toast->isVisible())
            placeToast();
        // A board-write toast ("◆ #K7Q2 · created card …") is itself a link: a press and the
        // release that lands back on the label open the card it names — the context gets first
        // refusal, exactly as a `#K7Q2` link in the output does — and the click replaces the
        // timer. Every other toast stays transparent to the mouse (showNextToast sets that).
        if (object == m_toast && !m_toastCardId.isEmpty()) {
            if (event->type() == QEvent::MouseButtonPress) { m_toastMousePress = true; return true; }
            if (event->type() == QEvent::MouseButtonRelease && m_toastMousePress) {
                const QString id = m_toastCardId;
                dismissToast();
                openOutputTarget(relay::links::cardTarget(id), -1, false);
                return true;
            }
        }
        if ((event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate) && object == window())
            noteWindowActivation(event->type() == QEvent::WindowActivate);
        // Minimised or restored: whether anyone can see this pane is what the shell poll's rate is
        // tuned on (card #057J), so it is re-tuned on the event rather than on the next tick —
        // coming back from the taskbar must not wait out a 400 ms quiet tick.
        if (event->type() == QEvent::WindowStateChange && object == window()) tunePoll();
        // Pane title (issue JRWQ): double click names this pane by hand; Esc leaves it alone.
        if (object == m_titleLabel && event->type() == QEvent::MouseButtonDblClick) {
            beginRename();
            hint(QStringLiteral("pane.rename"), QStringLiteral("Next time: /rename <name> · /rename-tab names the tab"));
            return true;
        }
        if (object == m_titleEdit && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            endRename();
            return true;
        }
        if (object == m_titleEdit && event->type() == QEvent::FocusOut) {
            endRename();
            return false;
        }
        // The folder line still opens the explorer on a click; headerDragEvent calls this when the
        // press and the release both land on it without a drag in between (issue #D60R).
        // Right-click anywhere in this pane's terminal: Relay's menu, not the engine's (issue
        // #X2F1). Real widgets inside the terminal, such as the engine's Find bar, keep theirs.
        //
        // The engine sends a proper context-menu event, and withholds it while a program is
        // reading the mouse, so that is the event to take.
        if (event->type() == QEvent::ContextMenu) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (ownsTerminalWidget(widget) && !acceptsTypedInput(widget)) {
                showTerminalMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
                return true;
            }
        }
        // Copy on select (off by default): after a left-button release that finishes a selection
        // in this pane's terminal, copy it to the clipboard.
        if (event->type() == QEvent::MouseButtonRelease && copyOnSelect()
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && ownsTerminalWidget(qobject_cast<QWidget *>(object))) {
            // Highlighting copies only a selection worth copying -- three letters or digits or
            // more (#C9VT), the same rule the shared filter applies, so a slip of the mouse does
            // not take the clipboard. An explicit copy (Ctrl+Shift+C, the context menu) was asked
            // for and still copies a short selection.
            QTimer::singleShot(0, this, [this] {
                if (m_backend && relay::copyOnSelectWorthCopying(m_backend->selectedText()))
                    copySelection();
            });
        }
        // The program transcript is a read-only text view, so its copy on select is the shared
        // filter (src/CopyOnSelect.h) installed where it is built. Ctrl+C in the prompt box still
        // copies the terminal's selection (copySelection).
        // A plain click in the terminal (not a selection drag) used to give it the keyboard.
        // It no longer does, so say which key still hands the keyboard over.
        if (event->type() == QEvent::MouseButtonPress && ownsTerminalWidget(qobject_cast<QWidget *>(object)))
            m_clickOrigin = static_cast<QMouseEvent *>(event)->pos();
        if (event->type() == QEvent::MouseButtonRelease && !m_native
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
            && ownsTerminalWidget(qobject_cast<QWidget *>(object))
            && (static_cast<QMouseEvent *>(event)->pos() - m_clickOrigin).manhattanLength() < 6)
            hint(QStringLiteral("terminal.click"), QStringLiteral("The prompt box is the input · %1 types into the terminal")
                     .arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
        // Card #XCXD: the row controls — × everywhere, the send arrow on the agent lane only —
        // are handled the same way on both lanes' viewports. A terminal row has no send arrow:
        // it is a command waiting for the shell, not a prompt waiting for the agent.
        QListWidget *queueLane = nullptr;
        if (m_queueList && object == m_queueList->viewport()) queueLane = m_queueList;
        else if (m_terminalQueueList && object == m_terminalQueueList->viewport()) queueLane = m_terminalQueueList;
        if (queueLane && event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            // The × at the right edge of a queue row removes it; on a steer it withdraws it. A steer
            // already being withdrawn has no × to click.
            const QPoint pos = static_cast<QMouseEvent *>(event)->pos();
            const QModelIndex index = queueLane->indexAt(pos);
            // Card #JDN4: ▾ opens a row to its whole text and ▴ folds it, without selecting it.
            if (index.isValid()) {
                const QRect rowRect = queueLane->visualRect(index);
                if (QueueRowDelegate::expandable(rowRect, index, queueLane->fontMetrics())
                    && QueueRowDelegate::expandRect(rowRect, index.data(QueueRowDelegate::SendNowRole).toBool(),
                                                    queueLane->fontMetrics()).contains(pos)) {
                    const QString rowId = index.data(QueueRowDelegate::RowIdRole).toString();
                    if (!m_expandedQueueRows.remove(rowId)) m_expandedQueueRows.insert(rowId);
                    QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
                    if (rowId.startsWith(QStringLiteral("entry:")))
                        hint(QStringLiteral("queue.expand.mouse"),
                             QStringLiteral("Next time: ↑ opens a queued item whole in the prompt box"));
                    return true;
                }
            }
            if (queueLane == m_queueList && index.isValid() && index.data(QueueRowDelegate::SendNowRole).toBool()
                && QueueRowDelegate::sendNowRect(queueLane->visualRect(index)).contains(pos)) {
                sendQueueRowNow(index.data(QueueRowDelegate::RowIdRole).toString());
                hint(QStringLiteral("queue.send-now.mouse"), relay::ShortcutHints::nextTime(
                    Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"))));
                return true;
            }
            if (index.isValid() && pos.x() >= queueLane->viewport()->width() - 26) {
                if (index.data(QueueRowDelegate::PendingRole).toBool()) return true;
                const bool steer = index.data(QueueRowDelegate::KindRole).toString() == QStringLiteral("steer");
                removeRow(index.data(QueueRowDelegate::RowIdRole).toString());
                if (steer) hint(QStringLiteral("queue.steer.remove.mouse"), relay::ShortcutHints::nextTime(QStringLiteral("↑ then Shift+Delete")));
                else hint(QStringLiteral("queue.remove.mouse"), QStringLiteral("Next time: ↑ opens a queued item in the prompt box, Shift+Delete removes, Ctrl+↑/↓ reorders"));
                return true;
            }
        }
        // The prompt box is the only keyboard input: clicking the terminal selects text, scrolls
        // and follows links, but anything that focuses the terminal surface hands the keyboard
        // straight back. Real widgets inside the terminal (the engine's Find bar) keep it.
        if (event->type() == QEvent::FocusIn && !m_native && m_composer && m_composer->isVisible()) {
            auto *focused = qobject_cast<QWidget *>(object);
            if (ownsTerminalWidget(focused) && !acceptsTypedInput(focused))
                QTimer::singleShot(0, this, [this] { if (!m_native) focusInput(); });
        }
        if (event->type() == QEvent::FocusIn && !m_opaqueProgram.isEmpty()) QTimer::singleShot(0, this, [this] { refreshProgramHint(); });
        // Esc in the masked prompt box gives up on answering: back to the normal prompt box.
        if (event->type() == QEvent::KeyPress && object == m_secretEdit && m_secretMode
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            m_secretDeclined = true;
            leaveSecretMode();
            toast(QStringLiteral("Masked input off · %1 types the password into the terminal")
                      .arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
            return true;
        }
        // Voice push-to-talk. The filter is on qApp, so only the pane holding the keyboard acts,
        // and the event is never consumed: Right Alt is AltGr on most layouts and must keep typing.
        // F9 is the exception — it is not a modifier, so it is swallowed while voice uses it.
        if (event->type() == QEvent::KeyPress && ownsKeyboard() && !static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
            // A real keystroke, not a modifier being held down on its own: the owner is typing
            // into this pane, so nobody else is driving it any more (section 10.3). The event is
            // passed on untouched — taking control back must never cost the key that did it.
            switch (static_cast<QKeyEvent *>(event)->key()) {
            case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
            case Qt::Key_AltGr: case Qt::Key_CapsLock: case Qt::Key_NumLock: break;
            default: takeBackFromGuest();
            }
        }
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && ownsKeyboard()) {
            auto *key = static_cast<QKeyEvent *>(event);
            const QString hold = voiceHoldKey();
            const bool isVoiceKey = voiceEnabled()
                && relay::voice::isHoldKey(hold, key->key(), key->nativeVirtualKey(), key->nativeScanCode());
            if (isVoiceKey && !key->isAutoRepeat()) {
                if (event->type() == QEvent::KeyPress) voiceKeyPressed(); else voiceKeyReleased();
                if (hold == QStringLiteral("f9")) return true;
            } else if (!isVoiceKey && event->type() == QEvent::KeyPress && m_voiceHold) {
                voiceInterrupted();
            }
        }
        if (event->type() == QEvent::KeyPress && object == m_editor) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (handleComposerKey(key)) return true;
            const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            // The composer is a few lines tall; PageUp/PageDown scroll the pane's terminal instead.
            if (mods == Qt::NoModifier && (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
                if (scrollTerminalPage(key->key() == Qt::Key_PageUp ? -1 : 1)) return true;
            }
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(object);
            // Terminal clipboard: Ctrl+C copies when text is selected, otherwise it reaches the
            // shell as an interrupt. Ctrl+V pastes at a shell prompt; inside a program such as
            // vim it is passed through (visual block). Ctrl+X always reaches the terminal.
            const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            if (event->type() == QEvent::KeyPress && ownsTerminalWidget(widget) && mods == Qt::ControlModifier && !key->isAutoRepeat()) {
                if (m_backend && key->key() == Qt::Key_C) {
                    if (copySelection()) return true;
                } else if (m_backend && key->key() == Qt::Key_V && !processBusy()) {
                    m_backend->paste();
                    return true;
                }
            }
            // Keys never reach the terminal outside native mode, so typing there can no longer
            // take Readline's line away from the composer; nothing to do here.
        }
        return QWidget::eventFilter(object, event);
    }

bool Pane::handleObservabilityEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("error")) {
            // Errors for protocol-11 requests carry the request id; keep them out of the transcript.
            const QString id = event.value(QStringLiteral("id")).toString();
            if (id.startsWith(QStringLiteral("skills-"))) {
                if (m_skillsDialog) m_skillsDialog->handleEvent(event); else status(event.value(QStringLiteral("text")).toString());
                return true;
            }
            if (id.startsWith(QStringLiteral("assist-"))) {
                onRouteAssisted({{"id", id}, {"route", QJsonValue()}, {"error", event.value(QStringLiteral("text")).toString()}});
                return true;
            }
            if (id.startsWith(QStringLiteral("turn-"))) {
                status(QStringLiteral("Turn details: ") + event.value(QStringLiteral("text")).toString());
                return true;
            }
            // The console ask failed (#83YV): the plugin refused (no kernel, console disabled,
            // python missing) or the workspace could not activate. The pane still needs a live
            // surface, so it falls back to the shell and says why. The ask is rearmed: the banner
            // action can try again once the cause is fixed.
            if (id.startsWith(QStringLiteral("console-")) && !m_consolePluginRequest.isEmpty() && !m_backend) {
                m_consoleAskSent = false;
                status(QStringLiteral("%1 console: %2 Started a shell instead. Use \"Restart shell\" to retry.")
                           .arg(consoleName(m_consolePluginRequest), event.value(QStringLiteral("text")).toString()));
                try {
                    startTerminal(m_cleanShell);
                } catch (const std::exception &) {
                    // The failed startTerminal already showed its banner.
                }
                return true;
            }
            // A fold asked for a call the worker no longer has ("Unknown turn_id (only the last 50
            // turns are kept)"): the fold says so, in one row, rather than staying empty (#TK9C).
            if (handleFoldError(id, event.value(QStringLiteral("text")).toString())) return true;
            if (handleInternalsError(id, event.value(QStringLiteral("text")).toString())) return true;
            // A local-model save that found nothing to detect (card #24XJ): the Settings pane's
            // section says so under the row that asked, never in the transcript.
            if (id.startsWith(QStringLiteral("lm-"))) {
                if (onLocalModelEvent) onLocalModelEvent(event);
                return true;
            }
            // The ⓘ view's requests (protocol 25): the view says what went wrong, in place.
            if (id.startsWith(QStringLiteral("info-"))) {
                if (m_infoView) m_infoView->setError(id, event.value(QStringLiteral("text")).toString());
                return true;
            }
            // The × on a steer lost the race: the turn took it (or gave it back) first. The
            // transcript or the queue already shows where it went, so this is only a status line.
            if (id.startsWith(QStringLiteral("withdraw-"))) {
                if (!m_withdrawnOnReturn.remove(id.mid(9)))
                    status(QStringLiteral("Too late to withdraw · the agent already had it"));
                return true;
            }
            // Saved window layout: the conversation this pane was restored with could not be
            // reopened (deleted, or written by another Relay). Start fresh with one line.
            if (!id.isEmpty() && id == m_restoreRequest) {
                m_restoreRequest.clear();
                m_restoreSession.clear();
                ensureLineStart();
                printInline(QStringLiteral("Previous conversation could not be reopened (%1) · starting a new one\n")
                                .arg(event.value(QStringLiteral("text")).toString()), Ink::Note);
                return true;
            }
            return false;
        }
        if (type == QStringLiteral("thinking_delta")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            QString &buffer = m_turnThinking[turn];
            if (buffer.size() < 200000) buffer += event.value(QStringLiteral("text")).toString();
            m_paneState.changed();   // pane_state (relay-terminal-71): the reasoning tail
            // "never" keeps the buffer (the turn pane still shows it) but draws nothing. With the
            // Activity pane open the block goes there and nothing is drawn here (#QT8C).
            if (m_internals) internalsThinking(turn, false, 0);
            else if (thinkingDisplay() != QLatin1String("never")) thinkingDelta(turn);
            pushThinkingToTurnPane(turn);   // a turn pane open on this turn follows the stream
            return true;
        }
        if (type == QStringLiteral("thinking_done")) {
            const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
            // The block is whole now: an open turn pane gets all of it, whatever was drawn here.
            pushThinkingToTurnPane(event.value(QStringLiteral("turn_id")).toString(), true);
            if (m_internals && m_thinkingAnchor.isEmpty()) {
                internalsThinking(event.value(QStringLiteral("turn_id")).toString(), true, ms);
                return true;
            }
            // A stream that stopped mid-reasoning reports {elapsed_ms: 0, chars: 0}: the fold still
            // gets what arrived, and its row says the stream stopped rather than lying with a time.
            if (!m_thinkingAnchor.isEmpty()) { finishThinkingFold(ms); return true; }
            // No fold is streaming ("never", a backend without folds, a program owning the screen):
            // the single ✦ line the transcript has always had.
            if (event.value(QStringLiteral("chars")).toInt() <= 0 && ms <= 0) return true;
            ensureLineStart();
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            const QString label = QStringLiteral("✦ thought for %1 s").arg(std::max<qint64>(1, (ms + 500) / 1000));
            if (turn.isEmpty()) {
                beginBlock(relay::gaps::Block::Call);   // the ✦ line sits with the tool rows (#TJBC)
                printInline(label + '\n', Ink::Note);
            }
            else printTurnLink(label, turn);   // the turn pane shows the reasoning in full
            return true;
        }
        if (type == QStringLiteral("turn_summary")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            QJsonObject history = event;
            history.insert(QStringLiteral("request"), m_currentRequest);
            m_turnSummaries.insert(turn, history);
            {
                // The picker's speed sort (owner, 2026-09-20): tokens per second as this pane
                // saw them — answer and thinking characters over four, across the whole turn,
                // tool time included. A proxy, not the provider's own count, and only from a
                // turn long enough to mean something.
                const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
                const qint64 chars = m_turnText.size() + event.value(QStringLiteral("thinking_chars")).toVariant().toLongLong();
                if (ms >= 2000 && chars >= 400)
                    relay::models::curation::noteSpeed(currentEntryKey(), (chars / 4.0) / (ms / 1000.0));
            }
            m_turnOrder.removeAll(turn); m_turnOrder.append(turn);
        while (m_turnOrder.size() > 50) {
            const QString old = m_turnOrder.takeFirst();
            m_turnSummaries.remove(old); m_turnThinking.remove(old); m_thinkingBlocks.remove(old);
        }
            const int tools = event.value(QStringLiteral("tools")).toArray().size();
            if (tools > 0) {
                endCallRun();   // a run of reads that ended the turn gets its newline here (#TK9C)
                const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
                printTurnLink(QStringLiteral("✦ %1 tool call%2 · %3 s").arg(tools).arg(tools == 1 ? QString() : QStringLiteral("s"))
                                  .arg(std::max<qint64>(1, (ms + 500) / 1000)), turn);
                // The lines above this one each hold their own detail. Taught once a turn has
                // printed some, with the key that opens the nearest one without the mouse.
                if (terminalFolds())
                    hint(QStringLiteral("call.fold"),
                         QStringLiteral("Click a ▸ line to unfold it here · Ctrl+Shift+Return unfolds the nearest"));
            }
            if (m_turnViews.contains(turn) && m_turnViews.value(turn)) m_turnViews.value(turn)->setSummary(event);
            return true;
        }
        if (type == QStringLiteral("turn_transcript")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            if (auto view = m_turnViews.value(turn)) view->setTranscript(event);
            return true;
        }
        // `tool_output` is also the live command-output stream ({text}); the reply to
        // tool_output_get is marked `stored: true` (protocol 11.1). Which of the two asked for it
        // is the request id: a `fold-` reply fills a fold in the terminal and opens no pane at all.
        if (type == QStringLiteral("tool_output") && event.value(QStringLiteral("stored")).toBool()) {
            if (handleFoldReply(event)) return true;
            if (handleInternalsReply(event)) return true;   // a row in the Activity pane asked (#QT8C)
            const QString turn = m_turnOutputRequests.take(event.value(QStringLiteral("id")).toString());
            if (!turn.isEmpty()) {
                if (auto view = m_turnViews.value(turn)) { view->setToolOutput(event); return true; }
            }
            openToolOutput(event);
            return true;
        }
        if (type == QStringLiteral("route_assisted")) {
            onRouteAssisted(event);
            return true;
        }
        // Settings › Local models (card #24XJ, protocol 28). A local endpoint has no key and no row
        // in the keys dialog, so its test result goes to the Settings pane's section instead.
        if (type.startsWith(QStringLiteral("local_"))
            || (type == QStringLiteral("key_tested")
                && event.value(QStringLiteral("preset")).toString().startsWith(QStringLiteral("local:")))) {
            if (onLocalModelEvent) onLocalModelEvent(event);
            return true;
        }
        if (type == QStringLiteral("key_tested") || type == QStringLiteral("key_removed")
            || type == QStringLiteral("agent_tools_imported")) {
            if (m_keysDialog && m_keysDialog->isVisible()) m_keysDialog->handleEvent(event);
            else if (type == QStringLiteral("agent_tools_imported")) {
                const int count = event.value(QStringLiteral("imported")).toArray().size();
                const int skipped = event.value(QStringLiteral("skipped")).toArray().size();
                QMessageBox::information(this, QStringLiteral("Import keys"),
                    QStringLiteral("Imported %1 API key(s). Skipped %2.").arg(count).arg(skipped));
                refreshPresets();
            }
            if (type == QStringLiteral("key_tested")) {
                // A guest's test is its login (35dcb6c4): one turn through its harness, so the
                // line names the tool and the model it answered on rather than a "key".
                const QString preset = event.value(QStringLiteral("preset")).toString();
                const QString guest = guestOfPreset(preset);
                const bool ok = event.value(QStringLiteral("ok")).toBool();
                if (!guest.isEmpty())
                    status(ok ? QStringLiteral("%1 answered on %2")
                                    .arg(guestName(guest),
                                         modelNameFor(preset, event.value(QStringLiteral("model")).toString()))
                              : guestName(guest) + QStringLiteral(": ") + event.value(QStringLiteral("error")).toString());
                else if (preset == QStringLiteral("relay-pro")) {
                    status(ok ? QStringLiteral("Relay Pro access is active.")
                              : QStringLiteral("Relay Pro: ") + event.value(QStringLiteral("error")).toString());
                    refreshPresets();
                } else
                    status(ok ? QStringLiteral("Key works for ") + preset
                              : QStringLiteral("Key test failed: ") + event.value(QStringLiteral("error")).toString());
                if (!guest.isEmpty()) refreshPresets();   // logged_in on the row follows the test
            }
            // Options › Models shows the same rows (owner, 2026-09-20): a removed key leaves the
            // provider unusable, so the fresh `presets` answer re-draws every Options pane.
            if (type == QStringLiteral("key_removed")) refreshPresets();
            return true;
        }
        if (type == QStringLiteral("skills") || type == QStringLiteral("skills_refined") || type == QStringLiteral("skills_import_preview")
            || type == QStringLiteral("skills_imported") || type == QStringLiteral("skills_updates")) {
            if (m_skillsDialog) m_skillsDialog->handleEvent(event);
            else if (type != QStringLiteral("skills")) status(QStringLiteral("Skills: ") + type);
            // The dialog's list is the fresher one after a refine or an import: `/name` follows it.
            if (type == QStringLiteral("skills")) setSkillCommands(event.value(QStringLiteral("items")).toArray());
            if (type == QStringLiteral("skills_refined")) {
                const auto items = event.value(QStringLiteral("items")).toArray();
                if (!items.isEmpty() && onOpenDocument) onOpenDocument(items.first().toObject().value(QStringLiteral("path")).toString());
            }
            return true;
        }
        return false;
    }

void Pane::updateSlashPopup() {
        // A queued item is in the box, not a command being typed: its leading "/" must not open the
        // popup, which would take the arrow keys the queue is using.
        if (inQueueSelection()) { hideSlashPopup(); return; }
        const QString text = m_editor ? m_editor->toPlainText() : QString();
        if (!m_editor || m_native || !text.startsWith('/') || text.contains(QRegularExpression(QStringLiteral("\\s")))) { hideSlashPopup(); return; }
        // The first `/` of a command is the cue to re-read the aliases, for the same reason the
        // palette does: they are files somebody else may have written (issue G8DK).
        if (text == QStringLiteral("/")) refreshAliases();
        const QString query = text.mid(1).toLower();
        struct Ranked { int score; int index; };
        QList<Ranked> ranked;
        // The built-ins, then the aliases (issue G8DK), so `/name` is discoverable and a built-in
        // is never hidden behind an alias of the same name.
        QList<SlashCommand> commands;
        QStringList builtins;
        for (const auto &command : slashCommands()) builtins << command.name;   // hidden ones shadow an alias too
        const QStringList shown = relay::slash::offered(builtins, hiddenSlashNames());
        // `/effort` offers this pane's model's levels, in the provider's order and words — the only
        // two rows of the table that are not the same on every pane (card #MDL1, 2026-09-21).
        const QString effortWhy = effortFixedReason();
        const QStringList effortLevels = offeredEfforts();
        for (const auto &command : slashCommands()) {
            if (!shown.contains(command.name)) continue;
            if (command.name == QStringLiteral("effort") || command.name == QStringLiteral("reasoning")) {
                SlashCommand row = command;
                if (!effortWhy.isEmpty()) { row.args.clear(); row.description = sentenceCase(effortWhy); }
                else if (!effortLevels.isEmpty()) row.args = QLatin1Char('[') + effortLevels.join(QLatin1Char('|')) + QLatin1Char(']');
                commands.append(row);
                continue;
            }
            commands.append(command);
        }
        for (const auto &alias : std::as_const(m_aliasList)) {
            if (alias.shadowed || builtins.contains(alias.name)) continue;
            commands.append({alias.name, alias.params.isEmpty() ? QString() : QStringLiteral("[args]"),
                             relay::aliases::paletteDetail(alias)});
        }
        // Then the skills, `/clean-commit` (feature intake 2026-09-18, "like warp"): last, so a
        // built-in or an alias of the same name keeps it, and `/skill <name>` still reaches the skill.
        QStringList taken;
        for (const auto &command : std::as_const(commands)) taken << command.name;
        for (const auto &skill : std::as_const(m_skillCommands)) {
            if (taken.contains(skill.name)) continue;
            QString description = skill.description;
            if (description.size() > 60) description = description.left(59).trimmed() + QChar(0x2026);
            commands.append({skill.name, skill.args, QStringLiteral("Skill · ") + description});
        }
        // Then what this console's context adds (card #PBZ4): an artifact console's plugin
        // commands, after every Relay-owned row and already namespaced where they collide.
        for (const auto &command : contextSlashCommands())
            commands.append({command.name, command.args,
                             QStringLiteral("✦ %1 · %2").arg(command.group, command.description)});
        // A guest catalog follows every Relay-owned surface. Relay's own names (and its aliases
        // and skills) win a collision, while a guest-only or newly introduced command passes
        // through verbatim rather than Relay diagnosing it as unknown.
        QStringList guestNames;
        int guestStart = -1;
        if (guestInFront()) {
            taken.clear();
            for (const auto &command : std::as_const(commands)) taken << command.name;
            guestStart = commands.size();
            for (const QString &slash : std::as_const(m_guestSlashCommands)) {
                const QString name = slash.mid(1);
                if (taken.contains(name, Qt::CaseInsensitive)) continue;
                commands.append({name, QString(), QStringLiteral("%1 · guest").arg(guestName(m_guest))});
                guestNames << name;
            }
        }
        for (int i = 0; i < commands.size(); ++i) {
            // Name prefix first, then names containing the query; descriptions do not match.
            const QString name = commands[i].name.toLower();
            int score = name.startsWith(query) ? 3000 - i : name.contains(query) ? 2000 - i : 0;
            // With nothing typed the scores follow declaration order, so the nine visible rows
            // would all be Relay's and the guest's commands — appended last — sat below the fold
            // no QA run or user would ever scroll to. While a guest is active the composer is
            // that guest's input line (§26.8): open the list with its commands. Typing a query
            // restores the usual order, where Relay's names win a tie.
            if (score > 0 && query.isEmpty() && guestStart >= 0 && i >= guestStart) score += 9000;
            if (score > 0) ranked.append({score, i});
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score > b.score; });
        if (ranked.isEmpty()) { hideSlashPopup(); return; }
        if (!m_slashList) {
            m_slashList = new QListWidget(this);
            m_slashList->setObjectName(QStringLiteral("atPicker"));
            m_slashList->setFocusPolicy(Qt::NoFocus);
            m_slashList->setUniformItemSizes(true);
            connect(m_slashList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_slashList->setCurrentItem(item); acceptSlashSelection(true); });
        }
        m_slashList->clear();
        for (const auto &entry : std::as_const(ranked)) {
            const auto &command = commands[entry.index];
            auto *item = new QListWidgetItem(QStringLiteral("/%1 %2    %3").arg(command.name, command.args, command.description).simplified(), m_slashList);
            item->setData(Qt::UserRole, command.name);
            item->setData(Qt::UserRole + 1, !command.args.isEmpty() && command.args.startsWith('<'));
            item->setData(Qt::UserRole + 2, guestNames.contains(command.name, Qt::CaseInsensitive));
        }
        m_slashList->setCurrentRow(0);
        if (m_composer) {
            const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
            const int rowHeight = std::max(18, m_slashList->sizeHintForRow(0));
            const int height = std::min(9, m_slashList->count()) * rowHeight + 8;
            m_slashList->setGeometry(composer.left() + 12, std::max(0, composer.top() - height - 4), std::min(640, composer.width() - 24), height);
        }
        m_slashList->show();
        m_slashList->raise();
        if (m_editor->ghost().size()) m_editor->setGhost(QString());
    }

void Pane::connectWorker() {
        m_workerConnected = true;
        connect(&m_worker, &QProcess::readyReadStandardOutput, this, [this] {
            // Nothing the worker says can matter to a pane that is being destroyed, and acting on
            // it is fatal. ~Pane sends `cancel` and `shutdown` and then calls waitForFinished(),
            // which pumps this very channel — so the worker's answer ran handle() from inside the
            // destructor, and one of those events reached rebuildQueueStrip(), which re-parented
            // widgets whose parents were already gone: SIGSEGV at 0x8 in
            // QWidgetPrivate::reparentFocusWidgets, on quit, 2026-09-20 04:39 (the frames are in
            // relay.log under `gui_crash`). The `finished` handler below has guarded this way all
            // along; this one did not.
            if (m_closing) return;
            m_workerBuffer += m_worker.readAllStandardOutput();
            if (m_workerBuffer.size() > 8 * 1024 * 1024) {
                m_worker.kill(); status(QStringLiteral("Worker protocol overflow; stopped.")); return;
            }
            int index;
            while ((index = m_workerBuffer.indexOf('\n')) >= 0) {
                const auto line = m_workerBuffer.left(index); m_workerBuffer.remove(0, index + 1);
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(line, &error);
                if (error.error == QJsonParseError::NoError && doc.isObject()) handle(doc.object());
            }
        });
        connect(&m_worker, &QProcess::readyReadStandardError, this, [this] {
            // No provider keys or arbitrary provider error bodies are logged.
            m_worker.readAllStandardError();
        });
        connect(&m_worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            // Same rule: ~Pane kills the worker when it will not stop, and a status line written
            // into a pane that is halfway through its own destructor is the same crash.
            if (m_closing) return;
            status(QStringLiteral("Local worker failed: ") + m_worker.errorString());
        });
        connect(&m_worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
            m_workerReady = false; m_configured = false; m_configuring = false; m_agentBusy = false;
            // The worker is gone (#R5TC): no roster to push, no device gate to hold, and the
            // pane looks idle to the directory — it cannot take a note until it restarts.
            m_paneNoHandoffTurn = false;
            relay::panedir::Directory::instance().wentIdle(m_token);
            relay::panedir::Directory::instance().rosterChanged();
            // A `route` that was in flight will never be answered. Left behind, m_pendingSubmit
            // is a one-submission guard that no reply can ever release, so every later terminal
            // submit in this pane is dropped in silence — the worst version of the bug the local
            // dispatch above exists to prevent.
            m_pendingSubmit.clear(); m_previewId.clear(); m_heldDecision = QJsonObject();
            m_recallPrompt = {};
            m_enterSteerBuffered = 0;   // a route that will never be answered takes its presses with it
            // Nobody is left to answer to (#MQ9C): take the ask down rather than leave the pane
            // asking on behalf of a worker that is gone.
            closeQuestion(QStringLiteral("the agent worker stopped"));
            stopTurnClock();
            const bool expectedExit = m_closing || m_restartConfigure;
            const QString exitReason = m_closing ? QStringLiteral("shutdown")
                : m_restartConfigure ? QStringLiteral("reconfigure") : QStringLiteral("unexpected");
            const auto logIdentity = [](const QString &value) {
                return QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$")).match(value).hasMatch()
                    ? value : QStringLiteral("unknown");
            };
            QString origin = qEnvironmentVariable("RELAY_LOG_ORIGIN", "interactive");
            if (origin != QStringLiteral("interactive") && origin != QStringLiteral("test")
                && origin != QStringLiteral("qa")) origin = QStringLiteral("unknown");
            const QString exitRecord = QStringLiteral("worker_exit pane=%1 code=%2 crashed=%3 reason=%4 expected=%5 origin=%6 run_id=%7 build_id=%8")
                .arg(paneLogId()).arg(code).arg(exit == QProcess::CrashExit ? 1 : 0)
                .arg(exitReason).arg(expectedExit ? 1 : 0).arg(origin)
                .arg(logIdentity(qEnvironmentVariable("RELAY_LOG_RUN_ID", QString::number(QCoreApplication::applicationPid()))))
                .arg(logIdentity(relay::buildinfo::running().id));
            if (expectedExit) relay::log::info(exitRecord);
            else relay::log::error(exitRecord);
            if (m_closing) {
                // Card #FYEY: the pane is going away with its worker; its land.py holds can be
                // reaped now (below, ~Pane covers a worker that never reports a finish).
                reapLandScripts();
                return;
            }
            if (m_restartConfigure) {
                QTimer::singleShot(250, this, [this] { if (!m_closing) startWorker(); });
                return;
            }
            const bool oom = isolation::takeResult(m_agentUnit) == QStringLiteral("oom-kill");
            const bool killed = exit == QProcess::CrashExit || code == 137 || code == 143;
            showBanner(oom ? QStringLiteral("The agent worker stopped because it ran out of memory (limit %1).")
                                 .arg(isolation::memory("isolation/agent_memory_max", isolation::agentDefault()))
                           : killed ? QStringLiteral("The agent worker was stopped.") : QStringLiteral("The agent worker exited."),
                       QStringLiteral("Restart agent"), [this] { hideBanner(); startWorker(); },
                       // Card #FYEY: restart from the checkout as it stands — startWorker pins
                       // from the current tree, so this loads a backend that has changed.
                       QStringLiteral("Reload backend"), [this] { hideBanner(); startWorker(); });
        });
        connect(&m_worker, &QProcess::started, this, [this] {
            // A fresh worker knows nothing (#J0VY): clear the gate so the first `app_catalog`
            // after `configure` reaches it even when its bytes are unchanged. (`configure`
            // embeds the catalog in its request too — this is belt and braces.)
            m_lastAppCatalog.clear();
            for (const auto &line : std::as_const(m_workerPending)) m_worker.write(line);
            m_workerPending.clear();
        });
    }

void Pane::startTerminal(bool cleanShell, const ConsoleProgram &program) {
        m_terminalRecords.resetGeneration();
        m_terminalStream.clear();
        // A console program names the pane while it runs (#83YV): the header chip, the routing
        // kind and the completion table all read this from here until the program exits.
        m_consoleProgram = program;
        updateHeader();
        // Set before the backend starts its shell so the child inherits these values.
        qputenv("RELAY_RUNTIME_DIR", m_runtime.path().toUtf8());
        qputenv("RELAY_SESSION_TOKEN", m_token.toUtf8());
        qputenv("RELAY_SHELL_EVENT", (m_data + QStringLiteral("/shell/event.py")).toUtf8());
        // The guest event channel (GT7X, protocol 26.3): the spool directory the shims a guest's
        // settings call (relay_core.guest_hook) write their events into, beside the runtime dir
        // above. Its presence in the environment is also what the installed hook commands test
        // before they run anything at all, so a claude started outside a pane costs nothing.
        const QString events = guestEventsDir();
        QDir().mkpath(events);
        QFile::setPermissions(events, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QDir().mkpath(guestAnswersDir());
        QFile::setPermissions(guestAnswersDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("RELAY_GUEST_EVENT", events.toUtf8());
        // The hook commands run the shim by absolute path (26.4), so no PYTHONPATH is needed for
        // them: "$RELAY_BACKEND_DIR/relay_core/guest_hook.py".
        const QString backendDir = m_data + QStringLiteral("/backend");
        qputenv("RELAY_BACKEND_DIR", backendDir.toUtf8());
        // PYTHONPATH still carries the backend so a user's own script can `import relay_core`.
        // It is appended, never prepended: the user's own PYTHONPATH keeps its order and cwd
        // still wins over both. Appended **only when it is not already there**: qputenv writes
        // Relay's own environment and startTerminal runs once per pane and once per shell
        // restart, so the plain append grew the variable by one copy every time (review of
        // 51587e3) — a hundred shell restarts, a hundred copies, in every child process.
        const QString pythonPath = qEnvironmentVariable("PYTHONPATH");
        if (!pythonPath.split(QDir::listSeparator(), Qt::SkipEmptyParts).contains(backendDir))
            qputenv("PYTHONPATH", (pythonPath.isEmpty() ? backendDir : pythonPath + QDir::listSeparator() + backendDir).toUtf8());
        qputenv("RELAY_PYTHON", m_python.toUtf8());
        qputenv("RELAY_CLEAN_SHELL", cleanShell ? "1" : "0");
        // Opt-in OSC 7 / OSC 133 marks (shell/relay-integration.bash). Relay's own engine
        // tracks the working directory and command boundaries from them.
        qputenv("RELAY_SHELL_INTEGRATION",
                QSettings().value(QStringLiteral("terminal/shell_integration"), false).toBool() ? "1" : "0");
        // The ssh and mosh wrappers (shell/integration.bash, card #S5SH) share the login's
        // connection through a socket here, so the agent can reuse it. Off in Options › Terminal.
        const bool wanted = QSettings().value(QStringLiteral("ssh/enhance"), QStringLiteral("auto")).toString() != QStringLiteral("off");
        QString why;
        const bool wrapSsh = wanted && sshSocketDirReady(&why);
        // A master killed outright leaves its socket behind; nothing else would ever remove it.
        // Once per run, before the first pane's shell: a socket someone answers on is left alone.
        static bool sweptSockets = false;
        if (wrapSsh && !sweptSockets) {
            sweptSockets = true;
            if (const int gone = relay::remote::pruneSockets(sshSocketDir()); gone > 0)
                relay::log::info(QStringLiteral("ssh_sockets_pruned pane=%1 count=%2").arg(paneLogId()).arg(gone));
        }
        if (wanted && !wrapSsh) {
            // Silence here reads later as "the agent cannot reach the host" with no reason given.
            relay::log::error(QStringLiteral("ssh_share_off pane=%1 reason=%2").arg(paneLogId(), why));
            m_sshShareProblem = why;
        }
        qputenv("RELAY_SSH_WRAP", wrapSsh ? "1" : "0");
        qputenv("RELAY_SSH_DIR", sshSocketDir().toUtf8());
        // The holder sessions of card #XQ8F (docs/SSH-AND-MOSH.md section 3b): this pane's ssh
        // land in a tmux session on their host, so the remote work survives disconnects, pane
        // closes and Relay restarts. Persistence is armed only while the wrapper can share a
        // connection at all; ssh/link chooses which transport a persistent login takes; and
        // ssh/hosts_never names hosts that never get one. The Options rows are the terminal
        // area's; the values are derived in relay::ssh so the rules are testable without a pane.
        qputenv("RELAY_SSH_PERSIST", relay::ssh::sshPersistValue(
                    wrapSsh, QSettings().value(QStringLiteral("ssh/persist"), true).toBool()).toUtf8());
        qputenv("RELAY_SSH_LINK", relay::ssh::sshLinkValue(
                    QSettings().value(QStringLiteral("ssh/link"), QStringLiteral("ssh")).toString()).toUtf8());
        qputenv("RELAY_SSH_NEVER", relay::ssh::sshNeverList(
                    QSettings().value(QStringLiteral("ssh/hosts_never")).toStringList())
                    .join(QLatin1Char(',')).toUtf8());
        // The Claude IDE bridge (GT7X, 26.9): the shell carries *no* bridge variables. They go on
        // the one command line the pane types when Claude Code is picked (`launchGuest`), so no
        // other program in this shell, and no shell started later, ever sees a port that may since
        // have gone away — the qputenv injection this replaced had to clear the keys on every
        // start for exactly that reason. The keys are still cleared here, once, because they may
        // be in Relay's own inherited environment (a Relay started from an IDE's terminal).
        for (const QString &key : relay::guestbridge::bridgeEnvKeys()) qunsetenv(key.toUtf8().constData());
        // Registered now, not at the first prompt, when a sidecar is running: a claude typed at
        // that prompt after a launch elsewhere must still find a routable pane (26.5).
        registerWithBridge();
        m_backendOwned.reset(relay::createTerminalBackend(m_engineCore, m_terminalHost));
        m_backend = m_backendOwned.get();
        m_terminal = m_backend->widget();
        m_terminalHost->layout()->addWidget(m_terminal);
        // Rows the terminal lets go of for good go to the pane's text journal (card #HEY7).
        m_paneJournal.attach(m_backend);
        // The prompt box is the only keyboard input: the terminal does not take focus on click.
        m_terminalFocusPolicy = Qt::NoFocus;
        applyTerminalFocusPolicy();
        m_backend->onFinished = [this](int) {
            m_backend = nullptr; m_terminal = nullptr; m_shellReady = false;
            // The console program left with the pty (#83YV): the chips, routing and completion
            // go back to reading whatever the pane runs next, which is usually nothing.
            m_consoleProgram = {};
            updateHeader();
            // The backend outlives this callback; drop it once the stack has unwound.
            QTimer::singleShot(0, this, [this] { if (!m_backend) m_backendOwned.reset(); });
            if (m_closing || m_restarting) return;
            // A shell stopped for memory (its scope's limit or systemd-oomd) keeps the pane open
            // with a restart banner. Any other exit closes the pane, like other terminals.
            const bool oom = isolation::takeResult(m_shellUnit) == QStringLiteral("oom-kill");
            if (oom) {
                showBanner(QStringLiteral("This pane's shell was stopped because it ran out of memory (limit %1).")
                               .arg(isolation::memory("isolation/shell_memory_max", isolation::shellDefault())),
                           QStringLiteral("Restart shell"), [this] { restartShell(); });
                return;
            }
            if (onShellExited) QTimer::singleShot(0, this, [this] { if (onShellExited) onShellExited(); });
        };
        // Alternate screen (vim, less, htop, tmux), reported by the emulator itself.
        m_backend->onAltScreenChanged = [this](bool active) { onPrimaryScreen(!active); };
        // OSC 7 from the shell integration (engine panes; see shell/relay-integration.bash).
        // A folder on another machine (OSC 7 from a shell behind ssh names its host) is the
        // login's, never this pane's local directory, even when the same path exists here.
        m_backend->onCwdHostChanged = [this](const QString &path, const QString &host) {
            if (m_login.active) {
                if (m_login.observedHost.isEmpty()) m_login.observedHost = host;
                if (host != m_login.observedHost) { m_login.identityReady = false; forgetLoginFiles(); }
                if (m_login.cwd != path) { hideAtPopup(); hideTabPopup(); m_login.cwd = path; updatePaths(); changed(); }
                publishLoginContext();
                return;
            }
            if (!relay::remote::isLocalHost(host, QSysInfo::machineHostName())) return;
            if (path.isEmpty() || path == m_cwd || !QFileInfo(path).isDir()) return;
            m_cwd = path; updatePaths(); changed();
        };
        // OSC 133 prompt marks: Relay keeps its own command state from the Bash bridge, so the
        // marks are only remembered here (engine panes use them to jump between prompts).
        m_backend->onShellIntegration = [this](const QString &confirmation) {
            if (!m_login.active || confirmation.section(';', 0, 0) != m_login.token) return;
            m_login.path = QString::fromUtf8(QByteArray::fromBase64(confirmation.section(';', 1).toLatin1()));
            if (!m_login.enhanced) {
                m_capture.clear(); // bootstrap bytes are presentation, not user output
                finishCommandCapture(-1); // the outer transport is no longer the command unit
                m_runningSince.invalidate(); m_commandLoaded = false;
                if (m_activeValid && !m_active.agent && m_active.remoteToken.isEmpty()) {
                    m_activeValid = false; m_activeLoaded = false;
                }
            }
            m_login.enhanced = true; m_login.identityReady = true;
            if (m_login.pendingExit >= 0) { const int code = m_login.pendingExit; m_login.pendingExit = -1; finishLoginCommand(code); }
            announceLoginFiles(); publishLoginContext();
        };
        m_backend->onPromptMark = [this](char kind, int exitCode) {
            m_lastPromptMark = kind;
            if (kind == 'D') m_lastMarkExitCode = exitCode;
            // A console's prompt is back (#83YV): what the agent said while a cell ran prints now,
            // after the parser has finished the chunk that drew the prompt.
            if (kind == 'B' && m_consoleProgram.isValid())
                QTimer::singleShot(0, this, [this] { flushInline(); });
            // Marks while a login owns the terminal come from the remote shell: they say exactly
            // when it is at its prompt, without waiting for the screen poll.
            if (m_login.active) {
                m_login.integration = true;
                if (kind == 'C') {
                    m_login.identityReady = false; forgetLoginFiles(); publishLoginContext();
                }
                if (kind == 'D') {
                    if (m_login.enhanced) m_login.pendingExit = exitCode;
                    else finishLoginCommand(exitCode);
                }
                updateLoginPrompt();
            }
        };
        // Every PTY chunk: the pane's terminal records (#TCXT) need native commands' output and
        // their authenticated boundaries, and the conversation index (protocol 14) Relay-run output.
        m_backend->setOutputCallbackEnabled(true);
        m_backend->onOutput = [this](const QByteArray &bytes) {
            captureTerminalBytes(bytes);
            // While a login runs, keep the row the host is writing. At its prompt that row is the
            // prompt, and Relay prints it back in its own colours after printing over it (#S5SH).
            if (m_login.active) {
                for (const char c : bytes) {
                    if (c == '\n' || c == '\r') m_login.line.clear();
                    else if (m_login.line.size() < 8192) m_login.line += c;
                }
            }
            // Live output only matters to a turn in flight (a `fresh` terminal_read): submission
            // and command completion sync on their own, so an idle pane resends nothing (#TCXT).
            if (m_terminalRecords.active() && (m_agentBusy || m_guestBusy)) {
                if (!m_terminalSyncPending) {
                    m_terminalSyncPending = true;
                    QTimer::singleShot(250, this, [this] { m_terminalSyncPending = false; syncTerminalContext(); });
                }
            }
            if (!m_capturing || m_capture.size() >= kCommandCaptureCap) return;
            m_capture.append(bytes.left(kCommandCaptureCap - m_capture.size()));
        };
        // Clickable paths (issue YZTK): a file opens in a preview pane at its line, a folder in
        // an explorer pane, a URL in the browser. The engine only reports paths that exist.
        m_backend->onLinkActivated = [this](const QString &target, int line, int column,
                                            Qt::KeyboardModifiers modifiers) {
            Q_UNUSED(column);
            // The engine links paths that exist here; inside a login the output is the remote
            // machine's, and the same path here is a different file (card #S5SH). URLs still open.
            // Alt+click is the exception (#7BYT): "add to the prompt box" applies to remote paths
            // too, and the mention resolves against the login's cwd, so it stays remote material.
            const QUrl url(target);
            if (m_login.active && !modifiers.testFlag(Qt::AltModifier)
                    && (url.scheme().isEmpty() || url.isLocalFile())) {
                openRemoteOutputPath(target, line);
                return;
            }
            openOutputTarget(target, line, true, modifiers);
        };
        // Alt+drag's finished selection (#7BYT): straight into the prompt box, fenced when it
        // spans lines. The focus stays here, so more material can be collected in a row.
        m_backend->onSelectionActivated = [this](const QString &text) {
            addSelectionToContext(text);
        };
        // Which paths in the output are real, and where a relative one is relative to (#S5SH):
        // this machine, until the pane is logged into another one — then the host's own answer,
        // from the batched cache in remoteLinkProbe(). Set once; the login is checked per call.
        m_backend->setLinkProbe([this](const QString &path) { return remoteLinkProbe(path); },
                                [this] { return m_login.active ? m_login.cwd : QString(); });
        // `#K7Q2` in the output is a card link when this pane's Switchboard index knows the id
        // (design section 5); the engine asks, the pane answers from the rows it has seen.
        m_backend->setCardLookup([this](const QString &id, QString *title) { return lookupOutputCard(id, title); });
        // Tool-call lines fold their detail open in place (#TK9C, docs/ENGINE.md "Folds"): every
        // OSC 8 URI under this prefix is an anchor the view owns, and a click on one that has no
        // content yet comes back here for it. A backend without the capability ignores both, and
        // the lines are anchored to relay://open-call instead (callAnchor).
        // OSC 52 (Options › Security, card #3KB7): a program — or a command the agent runs — may
        // put text on the system clipboard. Off unless the user turned it on, because output is
        // untrusted and would otherwise be able to replace what they are about to paste. Reads are
        // never answered and have no setting.
        applyClipboardPolicy();
        if (terminalCan(relay::TerminalBackend::Folds)) {
            m_backend->setFoldPrefix(QString(relay::calllines::kFoldPrefix));
            m_backend->onFoldRequested = [this](const QString &uri) { foldRequested(uri); };
        }
        // The Bash integration changes to this pane's directory after loading the user's
        // configuration, so a shell started elsewhere still lands where the pane says.
        // A console stops here (#AGNT). Everything above is the transcript surface — the engine,
        // the theme, the fold layer, the link probe, the card lookup — and a console keeps all of
        // it; what it does not have is a pty. This is the whole of "no shell": one early return,
        // not a second rendering path.
        if (!hasShell()) {
            m_oomKills = -1;
            return;
        }
        qputenv("RELAY_START_DIR", m_cwd.toUtf8());
#ifdef Q_OS_WIN
        const QStringList shell{relayPowerShell(), QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
            QStringLiteral("-NoExit"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), m_data + QStringLiteral("/shell/integration.ps1")};
#else
        const QStringList shell{relayBash(), QStringLiteral("--noprofile"),
            QStringLiteral("--rcfile"), m_data + QStringLiteral("/shell/integration.bash"), QStringLiteral("-i")};
#endif
        m_shellUnit.clear();
        bool started = false;
        // A pane gets the inline pyplot backend unless its parent explicitly chose a backend.
        // Use a child-only environment: other Relay processes and existing panes keep theirs.
        QStringList shellEnvironment;
        if (qEnvironmentVariableIsEmpty("MPLBACKEND"))
            shellEnvironment << QStringLiteral("MPLBACKEND=module://relay_mpl_backend");
        const QString scriptsPath = m_data + QStringLiteral("/scripts");
        const QString parentPythonPath = qEnvironmentVariable("PYTHONPATH");
        shellEnvironment << QStringLiteral("PYTHONPATH=") +
            (parentPythonPath.isEmpty() ? scriptsPath : parentPythonPath + QDir::listSeparator() + scriptsPath);
        // Child-only, like the pair above it: this pane's own id for the ssh/mosh wrapper of
        // #XQ8F, whose default holder name is "relay-" plus the first eight characters of it.
        // RELAY_SSH_PERSIST and friends are process-wide and so use qputenv in startTerminal;
        // this one is per pane. The id exists before any shell starts (the pane's token) and
        // initRestore takes the saved one back for the shells that follow, so a pane restored
        // from a layout still owns the holders named after it.
        shellEnvironment << QStringLiteral("RELAY_PANE_ID=") + scrollbackId();
        // This pane's own scratch session (#DVV2): TMPDIR puts mktemp in this shell — and in the
        // guest CLIs typed into it — inside <scratchRoot()>/<token>/tmp, built exactly the way
        // the backend builds a session's TMPDIR (scratch.session_root(<id>) / "tmp"; this pane's
        // token also reaches its worker as RELAY_SESSION_TOKEN). Owned and per pane, not /tmp.
        const QString scratchTmp = QDir(relay::scratchpaths::sessionRoot(m_token))
                .filePath(QStringLiteral("tmp"));
        if (QDir().mkpath(scratchTmp))   // a TMPDIR that is not there would break mktemp in the shell
            shellEnvironment << QStringLiteral("TMPDIR=") + scratchTmp;
        if (program.isValid()) {
            // A console program takes the pty the shell would have (#83YV). argv and any extra
            // environment come whole from the worker's `workspace_console` answer — `jupyter
            // console --existing <file>` attached to the kernel that worker just started — and
            // none of the shell machinery below applies. No integration bash: the bundled IPython
            // startup file emits the OSC 133 marks itself. No local holder: the console owns its
            // pty, and a restart reattaches through the connection file named in the argv, which
            // the worker keeps. No memory unit wrapping: the kernel it talks to lives in the
            // worker, not in this pty.
            QStringList environment = shellEnvironment;
            for (const QString &entry : program.env)
                if (!entry.isEmpty()) environment << entry;
            if (!m_backend->startProgram(program.argv.first(), program.argv.mid(1), m_cwd, environment))
                throw std::runtime_error(QStringLiteral("The console program could not be started: %1.")
                                             .arg(program.argv.join(QLatin1Char(' '))).toStdString());
            m_oomKills = -1;
            registerWithBridge();
            return;
        }
        // The local half of #87HB: with persistent local panes on, the pane's shell runs inside
        // the same holder a remote pane's does - a tmux session named after the pane's stable
        // scrollback id on Relay's own socket, so quitting or crashing Relay leaves the shell and
        // whatever it is running alive, and a restarted pane re-attaches to it. The session's
        // shell is the integration bash this pane would have run anyway, with this pane's
        // environment on the session command line: one tmux server serves every pane, so a new
        // session must not inherit whichever pane's client happened to start the server.
        // RELAY_HOLDER is what makes shell/integration.bash wrap its marks in tmux's DCS
        // passthrough, and the holder's conf turns allow-passthrough on. Memory isolation keeps
        // a pane's shell inside a unit that dies with it, which a server that outlives the pane
        // would defeat, so an isolated pane runs exactly as before - and so does a pane
        // restoring a local_login line, which re-attaches at the pane's first prompt instead
        // (initRestore), so the restored scrollback replays before the session's screen. The
        // holder script is not executable where Relay keeps it, so it runs through sh; it execs
        // tmux, which replaces the argv the pane shows.
        const QString holderSession = relay::ssh::localHolderName(scrollbackId());
        const bool underLocalHolder = !holderSession.isEmpty() && m_localLoginRestore.isEmpty()
            && QSettings().value(QStringLiteral("terminal/persistLocal"), true).toBool()
            && !(isolation::enabled() && isolation::available());
        QStringList sessionProgram = shell;
        if (underLocalHolder) {
            QStringList assignments;
            const auto quoted = [&assignments](const QString &entry) {
                assignments << entry.section(QLatin1Char('='), 0, 0) + QLatin1Char('=')
                    + relay::ssh::shellQuote(entry.mid(entry.indexOf(QLatin1Char('=')) + 1));
            };
            for (const QString &entry : std::as_const(shellEnvironment)) quoted(entry);
            // The process-wide RELAY_* words a shell needs, from this pane's own Relay: the tmux
            // server's environment is the first client's, which is another pane's.
            const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
            // The guests' Relay-owned home too (#5A37), or a tmux server a Relay without it
            // started would send `claude` in this pane back to ~/.claude.
            for (const char *name : {"RELAY_SESSION_TOKEN", "RELAY_RUNTIME_DIR", "RELAY_SHELL_EVENT",
                                     "RELAY_SHELL_INTEGRATION", "RELAY_CLEAN_SHELL", "RELAY_SSH_NEVER",
                                     "RELAY_GUEST_HOME", "CLAUDE_CONFIG_DIR", "CODEX_HOME",
                                     "RELAY_USER_CLAUDE_CONFIG_DIR", "RELAY_USER_CODEX_HOME"}) {
                const QString value = system.value(QLatin1String(name));
                if (!value.isEmpty()) quoted(QLatin1String(name) + QLatin1Char('=') + value);
            }
            quoted(QStringLiteral("RELAY_HOLDER=1"));
            QStringList words = assignments;
            for (const QString &word : shell) words << relay::ssh::shellQuote(word);
            sessionProgram = QStringList{QStringLiteral("/bin/sh"),
                m_data + QStringLiteral("/shell/remote-holder.sh"), holderSession, m_cwd, words.join(QLatin1Char(' '))};
        }
        if (isolation::enabled() && isolation::available()) {
            // OOMPolicy=continue (default): when a command exceeds the limit, the kernel stops that
            // command and the shell keeps running; Relay reports the kill from memory.events.
            // isolation/shell_oom_policy=stop ends the whole pane shell instead (restart banner).
            isolation::ensureTotalCeiling();   // #ZPWT: panes are generous; the slice bounds their sum
            m_shellUnit = QStringLiteral("relay-pane-%1-shell-%2").arg(m_token.left(8)).arg(++m_shellGeneration);
            const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
            started = m_backend->startProgram(tool, isolation::wrap(m_shellUnit,
                {QStringLiteral("MemoryMax=") + isolation::memory("isolation/shell_memory_max", isolation::shellDefault()),
                 QStringLiteral("MemoryHigh=") + isolation::memory("isolation/shell_memory_high",
                 isolation::fractionOf(isolation::memory("isolation/shell_memory_max", isolation::shellDefault()), 80)),
                 QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/shell_swap_max", isolation::shellSwapDefault()),
                 // Interactive bash ignores SIGTERM; SIGHUP ends it (and its jobs) when the scope stops.
                 QStringLiteral("KillSignal=SIGHUP"), QStringLiteral("TimeoutStopSec=5"),
                 QStringLiteral("OOMPolicy=") + (QSettings().value(QStringLiteral("isolation/shell_oom_policy")).toString() == QStringLiteral("stop")
                                                     ? QStringLiteral("stop") : QStringLiteral("continue"))}, shell), m_cwd,
                shellEnvironment);
        } else {
            if (isolation::enabled() && !s_isolationNoticeShown) {
                s_isolationNoticeShown = true;
                QTimer::singleShot(1500, this, [this] { status(QStringLiteral("Per-pane memory isolation is unavailable (no systemd user session); panes run unisolated.")); });
            }
            started = m_backend->startProgram(sessionProgram.first(), sessionProgram.mid(1), m_cwd,
                shellEnvironment);
        }
        if (!started) throw std::runtime_error("The pane's shell could not be started.");
        m_oomKills = -1;
        // Again, now that there is a shell pid to register: the first registration above happened
        // before the backend existed, and the sidecar routes a claude by the pid ancestry that
        // leads back to this shell (26.5).
        registerWithBridge();
    }

void Pane::askForConsoleProgram() {
        if (m_consolePluginRequest.isEmpty() || m_closing) return;
        if (m_consoleAskSent) return;
        m_consoleAskSent = true;
        status(QStringLiteral("Starting the %1 console…").arg(consoleName(m_consolePluginRequest)));
        // Protocol 36 (#83YV, #C0Q8): `console: true` makes the worker start the plugin's kernel
        // now and answer `workspace_console` with the argv a pane should run to attach to it.
        // plugin_id names the kernel plugin. Each terminal pane owns its worker, so its default
        // workspace is the one the agent and router also use. `workspace` is its folder: a worker with no
        // configured workspace refuses a task-plugin activation, and the pane's own folder is the
        // honest answer to "where". The id prefix is what handleObservabilityEvent's `error` arm
        // matches to fall back to a shell.
        send(QJsonObject{{QStringLiteral("type"), QStringLiteral("workspace_activate")},
                         {QStringLiteral("id"), QStringLiteral("console-") + m_token},
                         {QStringLiteral("plugin_id"), m_consolePluginRequest},
                         {QStringLiteral("workspace"), m_workspace},
                         {QStringLiteral("console"), true}});
        // A worker that never answers must not leave the pane a blank surface forever: past this,
        // the pane becomes a shell (and the ask is rearmed, so `Restart console` tries again).
        QTimer::singleShot(30000, this, [this] {
            if (m_closing || m_backend || m_consolePluginRequest.isEmpty() || m_consoleProgram.isValid()) return;
            status(QStringLiteral("%1 console: no answer from the workspace worker; started a shell instead. Use \"Restart shell\" to retry.")
                       .arg(consoleName(m_consolePluginRequest)));
            try {
                startTerminal(m_cleanShell);
            } catch (const std::exception &) {
                // A shell that cannot start either leaves the pane dead; the banner from the
                // failed startTerminal call is all the pane can say.
            }
        });
    }

void Pane::onWorkspaceConsole(const QJsonObject &event) {
        if (m_closing || m_consolePluginRequest.isEmpty() || m_backend) return;
        ConsoleProgram program;
        for (const QJsonValue &value : event.value(QStringLiteral("argv")).toArray())
            if (!value.toString().isEmpty()) program.argv << value.toString();
        program.kind = event.value(QStringLiteral("program")).toString();
        program.label = event.value(QStringLiteral("label")).toString();
        for (const QJsonValue &entry : event.value(QStringLiteral("completions")).toArray()) {
            const QJsonObject row = entry.toObject();
            const QString word = row.value(QStringLiteral("text")).toString();
            if (word.isEmpty()) continue;
            program.completions << word;
            program.completionLabels << row.value(QStringLiteral("description")).toString(word);
        }
        for (const QJsonValue &value : event.value(QStringLiteral("env")).toArray())
            if (!value.toString().isEmpty()) program.env << value.toString();
        if (!program.isValid()) {
            // The plugin answered without a program (python missing, console disabled): keep the
            // pane alive as a shell and say what the worker said, if anything.
            const QString note = event.value(QStringLiteral("note")).toString();
            status(note.isEmpty() ? QStringLiteral("%1 console unavailable; started a shell.")
                                  : QStringLiteral("%1").arg(note));
            try {
                startTerminal(m_cleanShell);
            } catch (const std::exception &) {
            }
            return;
        }
        try {
            startTerminal(m_cleanShell, program);
            const QString note = event.value(QStringLiteral("note")).toString();
            if (!note.isEmpty()) status(note);
        } catch (const std::exception &error) {
            status(QStringLiteral("The console program could not be started (%1); started a shell instead.").arg(QString::fromUtf8(error.what())));
            try {
                startTerminal(m_cleanShell);
            } catch (const std::exception &) {
            }
        }
    }

QString Pane::consoleName(const QString &pluginId) {
        // "relay.python" -> "Python"; anything else keeps its last segment, capitalised.
        QString name = pluginId.section(QLatin1Char('.'), -1);
        if (!name.isEmpty()) name[0] = name[0].toUpper();
        return name;
    }

void Pane::requestRoute(bool submit, const QString &overrideMode) {
        if (m_native) {
            if (submit) status(QStringLiteral("Native input is active. Press %1 or F12 to return to the prompt box.")
                                   .arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
            return;
        }
        if (submit) {
            // An ask is up: this text is the answer, not a prompt and not a command
            // (#MQ9C). Nothing below runs — not `@path`, not `/commands`, not the router. An
            // explicit terminal submit (Ctrl+Shift+Enter, or Enter with the chip on TERMINAL) is
            // the exception: a question from the agent must not take the user's terminal away,
            // and the ask's own footer says so.
            const bool toShell = (overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode)
                                 == QStringLiteral("shell");
            if (m_ask.open() && !toShell) {
                const QString typed = m_editor->toPlainText().trimmed();
                if (typed.isEmpty()) return;
                m_editor->remember(typed);
                m_editor->clear();
                answerQuestion(typed);
                return;
            }
            // `@path` on its own opens the file (or folder) in a Relay pane — but never a
            // picture: naming an image with `@` is one of the four ways it reaches the agent
            // (src/Images.h), and one pasted or dropped into an empty box leaves exactly one
            // `@` token as the whole draft. Enter sends that to the agent, as the attachment
            // line under it promises, instead of swallowing it into a preview pane.
            static const QRegularExpression only(QStringLiteral("^@(?:\"([^\"]+)\"|(\\S+))$"));
            const auto match = only.match(m_editor->toPlainText().trimmed());
            if (m_guest.isEmpty() && match.hasMatch()) {
                const QString path = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
                const QString absolute = m_login.active ? QString() : resolveComposerPath(path);
                if (!absolute.isEmpty() && !relay::images::isImageFile(absolute) && onOpenPath) {
                    m_editor->remember(m_editor->toPlainText().trimmed());
                    m_editor->clear();
                    hideAtPopup();
                    m_recentFiles.removeAll(absolute); m_recentFiles.prepend(absolute);
                    while (m_recentFiles.size() > 20) m_recentFiles.removeLast();
                    onOpenPath(absolute, 0);
                    return;
                }
            }
            if (m_atList && m_atList->isVisible()) hideAtPopup();
            // Aliases (issue G8DK), in the order they can appear: a template already in the box
            // submits as itself so the worker quotes the values, then `/name`, and finally — once
            // the mode is known, below — the name typed on its own in terminal mode.
            if (submitAliasFields()) return;
            if (tryRunSlashCommand(m_editor->toPlainText())) return;
            if (tryRunAliasSlash(m_editor->toPlainText())) return;
            if (tryRunSkillSlash(m_editor->toPlainText())) return;
            if (tryRunContextSlash(m_editor->toPlainText())) return;   // the context's own (#PBZ4)
            if (deferSlashUntilSkillCatalog(m_editor->toPlainText())) return;
            // A guest pane (26.8): the prompt box, the mode chip and the auto router work exactly
            // as in any other pane; only the delivery differs. A line that is a terminal command —
            // terminal mode, Ctrl+Shift+Enter, a typed `!`, or (below) the router's `shell`
            // verdict — is typed into the guest as `!<command>`, which both TUIs run as a local
            // shell line; a prompt is typed as the prompt (owner, 2026-09-19: "relay terminal
            // commands are piped to the agent as '! …' to maintain a seamless / identical
            // experience"). Relay's built-ins, aliases and skills above always win; any other
            // `/command` is the guest's own, never rejected by Relay. A guest the pane has moved
            // on from (a model was picked while it worked) gets nothing more: the line is the new
            // model's, exactly as after a native switch.
            if (guestInFront()) {
                QString line = m_editor->toPlainText();
                const QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
                const bool bang = line.startsWith(QLatin1Char('!'));
                if (bang) line = line.mid(1);
                if (bang || m_prefixMode == QStringLiteral("shell") || mode == QStringLiteral("shell")) {
                    if (line.trimmed().isEmpty()) {
                        status(QStringLiteral("Type a terminal command after !."));
                        return;
                    }
                    submitGuest(QLatin1Char('!') + line.trimmed(), true);
                    if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
                    return;
                }
                if (mode == QStringLiteral("agent") || m_prefixMode == QStringLiteral("agent") || !m_workerReady
                    || line.trimmed().startsWith(QLatin1Char('/'))) {
                    submitGuest(line, true);
                    if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
                    return;
                }
                // Auto: the local router decides below, and dispatch() delivers its verdict.
            }
            // A `/command` that is not one of the above never reaches the router: Relay says so
            // itself rather than letting Bash answer with "command not found".
            if (reportUnknownSlashCommand(m_editor->toPlainText())) return;
            clearAiGhost();
        } else if (const SlashCommand *command = slashCommandFor(m_editor->toPlainText())) {
            setRouteText(QStringLiteral("COMMAND · /%1 · %2").arg(command->name, command->description));
            return;
        } else if (const QString skill = skillFor(m_editor->toPlainText()); !skill.isEmpty()) {
            setRouteText(QStringLiteral("SKILL · /%1 · %2").arg(skill, skillCommand(skill)->description));
            return;
        } else if (const QString typed = m_editor->toPlainText().trimmed(); typed.startsWith(QLatin1Char('/'))
                   && relay::agent::findSlashCommand(contextSlashCommands(), typed.section(QLatin1Char(' '), 0, 0)) >= 0) {
            // A plugin command of this console's context (#PBZ4): say whose it is before Enter.
            const QList<relay::agent::ContextCommand> offered = contextSlashCommands();
            const auto &command = offered.at(relay::agent::findSlashCommand(offered, typed.section(QLatin1Char(' '), 0, 0)));
            setRouteText(QStringLiteral("COMMAND · /%1 · ✦ %2 · %3").arg(command.name, command.group, command.description));
            return;
        } else if (const QString name = relay::slash::attemptedName(m_editor->toPlainText());
                   !name.isEmpty() && !slashNames().contains(name)) {
            // The preview says it before Enter does: the label of a command Relay does not have.
            const QStringList close = relay::slash::closest(name, slashNames(), 2);
            setRouteText(close.isEmpty()
                             ? QStringLiteral("COMMAND · /%1 · not a Relay command · / for the list").arg(name)
                             : QStringLiteral("COMMAND · /%1 · not a Relay command · did you mean /%2?")
                                   .arg(name, close.join(QStringLiteral(" or /"))));
            return;
        }
        QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
        // An explicit agent submit never needed the worker's router (#N8VK): the verdict cannot
        // change where a forced-agent prompt goes, and the round trip made it refusable ("Local
        // router is not ready" while the worker was still starting) and losable ("Input changed
        // during routing") for a prompt whose destination was never in question. An explicit
        // *terminal* submit never needed it either, and that half was missed: with the worker gone
        // the whole prompt box went with it, `!echo …` included, although the shell was never the
        // worker's to lend. Both are dispatched locally below; only `auto`, which has a real
        // question for the router, is refused — and it says what is wrong and how to send anyway.
        const bool routerDown = !m_workerReady;
        // What this console is about gets the line before the pane does (#AGNT step 5). A card's
        // Enter travels as `board_ask` — the owner's words are written to the card's thread and
        // the stage advances before the model sees them (19.10) — so `CardContext::submit`
        // answers true and nothing below runs. A terminal context answers false, which is why a
        // terminal pane is byte-for-byte what it was. The text is cleared and remembered here,
        // exactly as `submitAgent` would have done, so the composer behaves the same either way.
        if (submit && m_context && !m_editor->toPlainText().trimmed().isEmpty()) {
            const QString typed = m_editor->toPlainText();
            // `overrideMode`, not the resolved `mode`: the raw route is which **key** was pressed
            // — "auto" for Enter, "agent" for Ctrl+Enter, "shell" for Ctrl+Shift+Enter — and a
            // card's three chords are exactly that distinction. Resolved, every console submit
            // would read "agent", because a console's mode is locked there, and Enter on a card
            // would plan instead of discuss.
            if (m_context->submit(overrideMode, typed)) {
                // What the context took, kept until the worker names the item it became: that is
                // what lets its row in the §12 strip be edited rather than only removed, because
                // the row itself carries a 120-character preview (card #CTRN).
                rememberContextSubmit(overrideMode, typed);
                // The box is the context's from here: it clears and remembers the line when it
                // took it, and **leaves it alone when it refused** — a card ask that arrives
                // while a cleanup is running keeps the words the owner typed, which is the whole
                // difference between "handled" and "consumed". The pane clearing it would throw
                // away a prompt nobody sent.
                if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
                return;
            }
        }
        if (submit && mode != QStringLiteral("agent")
            && (!m_pendingSubmit.isEmpty() || !m_heldDecision.isEmpty() || m_loading)) {
            // Card #XCXD: the box still holds the submitted draft while this route is in flight,
            // so a repeated Enter sees the same NONEMPTY text — it is the person pressing for
            // that prompt, not submitting something new. Count it: the second press steers the
            // prompt into the running turn, the third interrupts and sends it, and nothing after
            // that changes anything. The presses replay, exactly as counted, when the routing
            // resolves to an agent.
            if (m_editor->toPlainText() == m_submittedDraft && m_enterSteerBuffered < 2) ++m_enterSteerBuffered;
            return;
        }
        const QString id = QString::number(++m_requestId);
        // A program is blocked reading a line from the terminal: the prompt box answers it
        // instead of queueing a command (issue decision 5). Agent submissions still go to the
        // agent, so Ctrl+Enter and `*` keep working while a program waits.
        if (submit && sendLineToProgram(mode)) {
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
            return;
        }
        // PROGRAM mode never asks the router (protocol 36.2): its line goes to the program or
        // nowhere. The worker's router knows only auto/shell/agent, so a preview sent with
        // "program" came back as an "Unknown input mode." toast on every keystroke (#S976).
        if (mode == QStringLiteral("program")) {
            if (submit) status(QStringLiteral("No program is reading input here · the line stays in the box"));
            return;
        }
        // The alias name typed on its own. It needs the resolved mode, so it sits after the mode
        // is worked out and after a waiting program has had its line (issue G8DK).
        if (submit && tryRunAliasTyped(m_editor->toPlainText(), mode)) {
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
            return;
        }
        // The explicit agent submit itself (#N8VK): straight to submitAgent(), which starts the
        // turn at once whenever the agent is free (relay::queuesubmit) instead of waiting on a
        // route reply. The handoff flags are consumed here as dispatch() would consume them.
        if (submit && mode == QStringLiteral("agent")) {
            const QString typed = m_editor->toPlainText();
            if (typed.trimmed().isEmpty()) return;
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
            m_handoffPrefill = false; m_handoffPrefix = false; m_handoffOffered = false;
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            setRouteText(QStringLiteral("AGENT · explicit"));
            submitAgent(typed, true);
            return;
        }
        // The explicit terminal submit with no worker to ask. The router's only contribution to a
        // forced-shell line is the syntax check that offers a broken command to the agent; with no
        // agent to offer it to, the shell makes that judgement itself, exactly as it does for
        // anything typed natively. dispatch() then takes it down the ordinary terminal path —
        // login, queue, hand-off and all — so nothing else about the line changes.
        if (submit && routerDown
            && relay::input::withoutRouter(mode) == relay::input::WithoutRouter::Shell) {
            const QString typed = m_editor->toPlainText();
            if (typed.trimmed().isEmpty()) return;
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            setRouteText(QStringLiteral("TERMINAL · explicit"));
            m_submitMode = mode; m_submittedDraft = typed;
            dispatch(QJsonObject{{"route", QStringLiteral("shell")}, {"text", typed}, {"valid", true}}, mode);
            return;
        }
        // Everything left is `auto` (relay::input::WithoutRouter::Refuse — Shell and Agent were
        // taken above), and only the router can answer it. Say what is wrong, name the banner's
        // own action and the key that sends this very line to the terminal anyway.
        if (routerDown) {
            if (submit)
                status(relay::input::noRouterText(Keymap::instance().shortcutText(QStringLiteral("pane.restartShell")),
                                                  QStringLiteral("Ctrl+Shift+Enter")));
            return;
        }
        if (submit) {
            const QString typed = m_editor->toPlainText();
            if (typed.startsWith(QStringLiteral("/shell ")))
                hint(QStringLiteral("prefix.bang"), QStringLiteral("Next time: type ! at the start of the prompt for the terminal"));
            else if (typed.startsWith(QStringLiteral("/agent ")))
                hint(QStringLiteral("prefix.star"), QStringLiteral("Next time: type * at the start of the prompt for the agent"));
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            m_submitMode = mode;
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
            m_pendingSubmit = id; m_submittedDraft = typed;
            m_routedTerminalSnapshot = terminalSnapshot();
        } else m_previewId = id;
        QJsonObject route{{"type", "route"}, {"id", id}, {"text", m_editor->toPlainText()}, {"mode", mode},
                          {"known_commands", m_knownCommands}, {"path", m_shellPath}, {"cwd", m_cwd}};
        const QStringList foreground = m_consoleProgram.isValid()
            ? QStringList{m_consoleProgram.kind} : foregroundArgv();
        if (!foreground.isEmpty())
            route.insert(QStringLiteral("foreground_program"), QJsonArray::fromStringList(foreground));
        // At a login the local PATH and aliases describe the wrong machine: the router only
        // decides between a line for the remote shell and a request for the agent (#S5SH).
        if (loginTakesLines()) route.insert(QStringLiteral("remote"), QJsonObject{{"host", loginHost()}});
        send(route);
    }

void Pane::dispatch(const QJsonObject &decision, const QString &mode) {
        const QJsonObject capturedTerminal = m_routedTerminalSnapshot;
        m_routedTerminalSnapshot = {};
        const QString route = decision.value(QStringLiteral("route")).toString();
        const QString text = decision.value(QStringLiteral("text")).toString();
        if (route == QStringLiteral("empty")) { m_enterSteerBuffered = 0; return; }
        if (route == QStringLiteral("incomplete")) {
            m_enterSteerBuffered = 0;
            status(QStringLiteral("Incomplete input · keep typing"));
            return;
        }
        if (route == QStringLiteral("program")) {
            m_enterSteerBuffered = 0;
            const QString line = m_submittedDraft.isEmpty() ? text : m_submittedDraft;
            sendLineToProgram(QStringLiteral("program"), &line);
            return;
        }
        // Card #XCXD: the Enters pressed while this route was in flight replay now, exactly as
        // counted — second press steers the prompt into the running turn, third interrupts — and
        // only when the verdict really routed the text to the agent. A shell or guest verdict
        // has no agent turn to steer into, so its presses are spent here instead of firing a
        // spurious steer. The singleShot lands after this submit is queued, which is the prompt
        // the presses were for; and a free agent ran the prompt as its own turn, leaving nothing
        // to steer.
        const int replayEnters = route == QStringLiteral("agent") && !guestInFront()
                                     ? qMin(m_enterSteerBuffered, 2) : 0;
        m_enterSteerBuffered = 0;
        if (replayEnters > 0)
            QTimer::singleShot(0, this, [this, replayEnters] {
                if (!m_agentBusy) return;
                for (int i = 0; i < replayEnters; ++i) emptyEnterSteerSequence();
            });
        // A command the agent put in the prompt box (protocol 22): its exit goes back to the agent,
        // which replaces the fix loop for this one submission.
        const bool handoff = m_handoffPrefill;
        m_handoffPrefill = false; m_handoffPrefix = false; m_handoffOffered = false;
        // A guest pane (26.8): the auto router's verdict says how the line is typed into the guest
        // — a command as `!<command>`, a prompt as itself. Nothing reaches the pane's own shell.
        if (guestInFront() && (route == QStringLiteral("shell") || route == QStringLiteral("agent"))) {
            submitGuest(route == QStringLiteral("shell") ? QLatin1Char('!') + text.trimmed() : text, true);
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
            return;
        }
        if (route == QStringLiteral("shell") && m_login.active && m_altScreen && !loginAtPrompt()) {
            // Logged in, but a full-screen program on the host has the keyboard. Queueing this for
            // the local shell would run it on the wrong machine (#S5SH): the text stays in the box.
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("control.human"));
            status(QStringLiteral("%1 has the terminal · %2 types into it, or wait for its prompt")
                       .arg(foregroundProgramName(), keys));
            toast(QStringLiteral("Not sent · a full-screen program on %1 has the terminal · %2")
                      .arg(loginHost(), keys));
            return;
        }
        if (route == QStringLiteral("shell")) {
            const bool valid = decision.value(QStringLiteral("valid")).toBool(decision.value(QStringLiteral("syntax_ok")).toBool(true));
            const QString problem = decision.value(QStringLiteral("invalid_reason")).toString(
                decision.value(QStringLiteral("syntax_error")).toString());
            // Wrong-mode hints: agent_signal says the text reads like a request, not a command.
            const bool readsLikeRequest = decision.value(QStringLiteral("agent_signal")).toBool();
            if (mode == QStringLiteral("shell")) {
                // Terminal mode (Ctrl+Shift+Enter): always the terminal. An invalid command
                // goes to the agent to be fixed; a failing run is fixed and re-run.
                if (!valid && readsLikeRequest) {
                    // Wrong mode, not a broken command: nothing runs and nothing is cleared, so
                    // Ctrl+I followed by Enter resubmits the same text to the agent.
                    wrongModeHint(true);
                    ensureLineStart();
                    printInline(QStringLiteral("✗ %1 · this reads like a request for the agent, not a command\n")
                                    .arg(problem.isEmpty() ? QStringLiteral("not a valid command") : problem),
                                Ink::Error);
                    closeInline();
                    return;
                }
                if (!valid) {
                    const QString command = text.trimmed().section(QLatin1Char(' '), 0, 0);
                    // The shell's `ready` event lists only its aliases and functions, so the
                    // programs on its PATH (`git` for `gti`) are read here, once per typo, and
                    // only names within two characters of the typed length can be close.
                    QStringList commands = knownCommandNames();
                    for (const QString &dir : m_shellPath.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
                        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Executable);
                        for (const QFileInfo &entry : entries)
                            if (std::abs(int(entry.fileName().size() - command.size())) <= 2)
                                commands << entry.fileName();
                    }
                    commands.removeDuplicates();
                    // A transposed pair is the common short typo (`gti` for `git`), and it goes
                    // first: the slash ranker allows one edit for names this short, which on a
                    // real PATH also reaches `gio`.
                    QString suggestion;
                    for (int i = 0; i + 1 < command.size(); ++i) {
                        QString swapped = command;
                        const QChar first = swapped.at(i);
                        swapped[i] = swapped.at(i + 1);
                        swapped[i + 1] = first;
                        if (swapped != command && commands.contains(swapped)) { suggestion = swapped; break; }
                    }
                    if (suggestion.isEmpty()) suggestion = relay::slash::closest(command, commands, 1).value(0);
                    ensureLineStart();
                    printInline(relay::input::invalidTerminalLine(problem, suggestion), Ink::Error);
                    closeInline();
                    return;
                }
                m_editor->remember(text); m_editor->clear();
                submitTerminal(text, !handoff, readsLikeRequest, handoff);
                return;
            }
            if (!valid) {
                // Forwarded to the agent anyway: the note is explain_invalid's call here too, so a
                // route-assist-rewritten or legacy shell-routed prose line cannot print
                // "command not found" under its echo (card #EB4A). An older worker that does not
                // send the field keeps the note.
                submitAgent(text, true,
                            decision.value(QStringLiteral("explain_invalid")).toBool(true) ? problem : QString(),
                            QString(), QString(), capturedTerminal);
                return;
            }
            submitTerminal(text, false, false, handoff);
        } else {
            // "agent", or a legacy "ambiguous" decision: the agent is the default for invalid input.
            // Show why non-command input went to the agent, e.g. "command not found: foo" — but only
            // when the line reads as an attempt at a command. Under a plain request the note reads as
            // the failure of a command the user never meant to run ("symlink from ~/projects to
            // here", answered fine, with "command not found: symlink" under it — owner report,
            // 2026-09-18). Which lines qualify is router.explain_invalid's call, next to the rest of
            // the language rules; an older worker that does not send it keeps the note.
            QString why = !decision.value(QStringLiteral("valid")).toBool(true) && mode != QStringLiteral("agent")
                              && decision.value(QStringLiteral("explain_invalid")).toBool(true)
                ? decision.value(QStringLiteral("invalid_reason")).toString() : QString();
            // Wrong-mode hints: remember a runnable command submitted in agent mode, so a failing
            // run_command of the same text can suggest the terminal (see the tool_result handler).
            const QString shellText = mode == QStringLiteral("agent")
                                      && decision.value(QStringLiteral("valid")).toBool(true)
                                      && !decision.value(QStringLiteral("agent_signal")).toBool()
                                      ? text : QString();
            submitAgent(text, true, why, QString(), shellText, capturedTerminal);
        }
    }

void Pane::modelBoxPicked(const QString &data) {
        logModelPicker(QStringLiteral("pick"), data);
        // Owner report, 2026-09-18: "selecting model options in the model dropdown didnt do
        // anything. the main use case for that is going to be swapping between the main and
        // flash models." Two entries in this list are not models: the gear (the model options
        // modal, which lost its branch here when the strip was rebuilt) and the Main/Flash rows
        // (the pane's agent role). Both are handled before selectModel, which only knows presets.
        if (data == QStringLiteral("gear:modelOptions")) {
            refreshPickers();   // put the box back on the pane's model: the gear is not a choice
            openModelPicker(relay::ModelsPane::providersTab());
            hint(QStringLiteral("model.options.mouse"), QStringLiteral("Tip: /models opens the models pane's providers tab from the prompt box"));
            return;
        }
        // "all models" (owner, 2026-09-23, card #BXMS: "all models -> opens the full scrollable
        // list"): the same box again, open on every class whole and every other available model —
        // the list a typed filter searches. Picking there is an ordinary box pick.
        if (data == relay::modelrows::allModelsData()) {
            refreshPickers();
            m_boxAllPending = true;
            QTimer::singleShot(0, this, [this] {
                if (!m_modelBox) return;
                m_modelBox->setFocus(Qt::ShortcutFocusReason);
                m_modelBox->showPopup();
            });
            return;
        }
        if (data == QStringLiteral("gear:picker")) {
            refreshPickers();
            openModelPicker();
            hint(QStringLiteral("model.picker.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.modelOptions")), QStringLiteral("the models pane")));
            return;
        }
        // A model of one mode's list (card #MDL1, section 5.1): "this pane, this mode, that
        // model". Enter on such a row switches the mode too when the box was turned to another
        // page — which is the whole point of Left and Right — and the model it names is this
        // pane's own for that mode from then on, without touching the tier list, which belongs to
        // every other pane (protocol 13.5).
        if (data.startsWith(QStringLiteral("pick:"))) {
            const QString rest = data.mid(5);
            const int bar = rest.indexOf(QLatin1Char('|'));
            if (bar <= 0) return;
            const QString mode = rest.left(bar);
            const QString key = rest.mid(bar + 1);
            const QString role = modeRoleOf(mode);
            if (mode == QStringLiteral("main")) {
                // Main is the pane's own model: the ordinary pick, and it puts the pane back on
                // its main role if a mode had taken it off.
                hintSwapForPick(key);
                selectEntry(key);
            } else {
                // The level the tier lists give that entry is the one it runs at here, and the one
                // the pane comes back on after a restart.
                setAgentRole(role, true,
                             relay::modelrows::ModePick{key, relay::models::curation::listEffortFor(key)});
            }
            focusInput();
            hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
            return;
        }
        // A catalog entry (owner, 2026-09-20): a provider and one of its models.
        if (data.startsWith(QStringLiteral("entry:"))) {
            hintSwapForPick(data.mid(6));
            selectEntry(data.mid(6));
            focusInput();
            hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
            return;
        }
        if (data.startsWith(QStringLiteral("role:"))) {
            const QString role = data.mid(5);
            chooseAgentRole(role); focusInput();
            // The Local agent has no shortcut on purpose (it takes no key), so its hint names
            // the fast path it does have: /local in the prompt box. WARP.md, "Shortcut hints".
            if (role == QStringLiteral("local"))
                hint(QStringLiteral("model.role.local.mouse"),
                     QStringLiteral("Tip: /local runs this pane on the local model, /main goes back"));
            else
                hint(QStringLiteral("model.role.mouse"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.flashAgent")),
                                                    QStringLiteral("the main and flash models")));
            return;
        }
        // Claude Code or Codex: the guest is this pane's agent from here on — through its harness
        // when the worker offers one (29.4), as a TUI in this pane's shell when it does not (26.9).
        // Both rows carry the same `guest:<id>` data, and pickGuest decides which.
        if (data.startsWith(QStringLiteral("guest:"))) {
            const QString id = data.mid(6);
            pickGuest(id);
            focusInput();
            hint(QStringLiteral("model.guest.mouse"),
                 QStringLiteral("Tip: /model %1 does this from the prompt box").arg(id));
            return;
        }
        // A preset picked while a guest runs: the pane shifts over at once, like any model switch
        // (leaveGuest); the guest finishes what it is doing and is then asked to exit.
        if (guestInFront()) {
            leaveGuest([this, data] { selectModel(data); });
            focusInput();
            return;
        }
        selectModel(data); focusInput();
        hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
    }

void Pane::refreshPickers() {
        // A restored pick whose entry has left the catalog or lost its key is dropped, silently:
        // the mode reads rank 1 of its list again, which is what it would have done had the pick
        // never been made. Asked once, at the first refresh that has a catalog to ask.
        if (!m_modePicksChecked) {
            const relay::models::Catalog catalog = modelCatalog();
            if (!catalog.entries.isEmpty()) {
                m_modePick = relay::modelrows::usableModePicks(catalog, m_modePick);
                m_modePicksChecked = true;
            }
        }
        m_paneState.changed();   // pane_state (relay-terminal-71): model and mode; changed() runs this
        if (!m_modelBox) return;
        const QSignalBlocker modelBlock(m_modelBox);
        if (m_modeChip) {
            m_modeChip->setText(m_modeValue == QStringLiteral("program") ? QStringLiteral("PROGRAM · %1").arg(foregroundProgramName())
                                : m_modeValue == QStringLiteral("shell") ? shellModeLabel()
                                : m_modeValue == QStringLiteral("agent") ? QStringLiteral("agent")
                                                                         : QStringLiteral("auto"));
            m_modeChip->setToolTip(QStringLiteral("Where this line goes (%1 cycles). %2")
                                       .arg(Keymap::instance().shortcutText(QStringLiteral("input.toggle")),
                                            m_routeLabel ? m_routeLabel->toolTip() : QString()).trimmed());
        }
        // The rows are relay::modelrows' (src/ModelRows.h, cards #PK5Q and #MDL1): the modes, then
        // the models of the mode this pane is in, then "all models" and "model settings". Every console's
        // box draws the same list from the same builder, because the owner asked for exactly that
        // on 2026-09-20 — "can you have the picker be the same as in the main terminal". What is
        // decided here is only what this pane alone knows: which mode it is in, what it picked for
        // each mode, the guest running as a TUI in its shell, and the model serving this one turn.
        const relay::modelrows::Context rows = modelRowsContext();
        const QString liveGuest = m_guestLeaving ? QString() : m_guest.isEmpty() ? m_guestWanted : m_guest;
        relay::modelrows::fill(m_modelBox, rows);
        // The rows are read again the moment the list is about to be drawn, so the box always
        // opens on this pane's own model with no class expanded (card #MDL1, design 5.3).
        m_modelBox->onRows = [this](int *current) {
            // Every opening is the short list again unless it is the one "all models" asked for.
            m_boxAll = m_boxAllPending;
            m_boxAllPending = false;
            const auto rows = modelBoxRows(current);
            QJsonArray logged;
            for (const auto &row : rows)
                logged.append(QJsonObject{{"text", row.text}, {"data", row.data}, {"enabled", row.enabled}});
            logModelPicker(QStringLiteral("open"), QString(), logged);
            return rows;
        };
        m_modelBox->onExpandKey = [this](const QString &klass, int delta) { return expandModelClass(klass, delta); };
        m_modelBox->onQueryRows = [this](const QString &) { return modelBoxFilterRows(); };
        m_modelBox->onPickedData = [this](const QString &data) { modelBoxPicked(data); };
        // The chip is **the model alone**, on every mode (owner, 2026-09-21: "no need to show the
        // model class in the pane header"), while the row it sits on carries its class's header
        // above it: that is what CurrentTextComboBox::setCollapsedText is for. The mode is in the
        // box's tooltip instead.
        m_modelBox->setCollapsedText(m_serving.isEmpty() ? relay::modelrows::collapsedText(rows) : QString());
        if (m_presets.isEmpty() && m_currentPreset.isEmpty())
            m_modelBox->setCollapsedText(QStringLiteral("Loading models…"));
        m_modelBox->setEnabled(true);
        // The model actually serving the turn, when it is not the pane's own (C5): plan mode's
        // planning model, an image turn's vision model, or the provider a failover moved to. One
        // row for all three, marked with what moved it, so an answer never seems to have come from
        // the pane's own model; the tooltip says the pane gets that model back after the turn.
        if (!m_serving.isEmpty()) {
            const Serving &serving = m_serving.last();
            m_modelBox->insertItem(0, QStringLiteral("%1 %2 · this turn").arg(servingMark(serving.why), servingModelName(serving)),
                                   QStringLiteral("serving:") + serving.model);
            m_modelBox->setCurrentIndex(0);
        }
        const QString onList = relay::modelrows::collapsedTooltip(rows);
        m_modelBox->setToolTip(modelTooltip(!m_serving.isEmpty()
            ? servingTooltip()
            : !onList.isEmpty() && liveGuest.isEmpty() && !onGuestPreset()
            // The mode left the chip with design 5.3, so this is where it is said.
            ? onList
            : !liveGuest.isEmpty()
            ? QStringLiteral("%1 is this pane's agent: the prompt box is its input. Pick a model to leave it.").arg(guestName(liveGuest))
            : onGuestPreset()
            // Tier A (29.4): the guest answers through its harness, so the conversation, the chips
            // and the call lines are Relay's and the terminal below is still the user's own shell.
            ? QStringLiteral("%1 is this pane's agent, through its own harness%2. The terminal stays yours.")
                  .arg(guestName(guestOfPreset(m_currentPreset)),
                       m_guestSession.isEmpty() ? QString() : QStringLiteral(" (session %1)").arg(m_guestSession.left(8)))
            : QString()));
        m_modelBox->updateGeometry();   // the collapsed box's width follows the new current row
        const QString state = m_modelBox->displayText() + QLatin1Char('|') + m_agentRole
            + QLatin1Char('|') + m_currentPreset + QLatin1Char('|') + m_model + QLatin1Char('|') + m_effort;
        if (state != m_loggedPickerState) {
            logModelPicker(m_loggedPickerState.isEmpty() ? QStringLiteral("initial") : QStringLiteral("changed"));
            m_loggedPickerState = state;
        }
    }

void Pane::launchGuest(const QString &guest, const QStringList &extra, const QString &cwd) {
        if (guestDisplayName(guest) == QStringLiteral("The guest agent")) {
            status(QStringLiteral("Unknown guest agent: %1").arg(guest));
            return;
        }
        if (m_guestLaunch) { status(QStringLiteral("A guest is already starting in this pane.")); return; }
        if (m_agentBusy) stopAgent();
        int port = 0;
        if (guest == QStringLiteral("claude")) {
            // The first claude launch of the run starts the sidecar (26.5); the port then goes on
            // this one command line. A bridge that failed to start costs nothing: no port, no diffs.
            auto &bridge = relay::guestbridge::Bridge::instance();
            bridge.setDataRoot(m_data);
            port = bridge.portFor(m_python, m_token);
            registerWithBridge();
        }
        const QString directory = cwd.isEmpty() ? m_cwd : cwd;
        auto *launch = new QProcess(this);
        launch->setWorkingDirectory(QFileInfo(directory).isDir() ? directory : m_cwd);
        launch->setProcessEnvironment(guestHelperEnvironment());
        launch->setProgram(m_python);
        QStringList arguments{QStringLiteral("-X"), QStringLiteral("utf8"), QStringLiteral("-m"), QStringLiteral("relay_core.guest_launch"), guest,
                              QStringLiteral("--runtime-dir"), m_runtime.path(), QStringLiteral("--cwd"), directory,
                              QStringLiteral("--port"), QString::number(port), QStringLiteral("--python"), m_python};
        // The same defaults as the harness route (Options › Claude Code and Codex), as the CLI's
        // own flags on the launch line; `guest_launch` leaves them out when `extra` names its own.
        // Not the permission posture: a TUI guest asks in its own terminal, where the user answers
        // it directly, so the launch's bypass flags are the owner's rule and nothing narrows them
        // (29.3 — the setting is the harness route's, and the row says so).
        if (const QString model = guestSetting(guest, QStringLiteral("model")); !model.isEmpty())
            arguments << QStringLiteral("--model") << model;
        if (const QString effort = guestSetting(guest, QStringLiteral("effort")); !effort.isEmpty())
            arguments << QStringLiteral("--effort") << effort;
        // Options › Privacy "Guests use memory from" (#MEMS); unset is `relay`, guest_launch's default.
        if (const QString memory = QSettings().value(QStringLiteral("guests/memory")).toString().trimmed(); !memory.isEmpty())
            arguments << QStringLiteral("--memory") << memory;
        if (!extra.isEmpty()) arguments << QStringLiteral("--") << extra;
        launch->setArguments(arguments);
        m_guestLaunch = launch;
        m_guestWanted = guest;   // the picker shows the row from now until the detector agrees
        refreshPickers();
        connect(launch, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, launch, guest, cwd](int code, QProcess::ExitStatus exit) {
                    launch->deleteLater();
                    if (m_guestLaunch == launch) m_guestLaunch = nullptr;
                    const QJsonObject result = QJsonDocument::fromJson(launch->readAllStandardOutput()).object();
                    if (exit != QProcess::NormalExit || code != 0 || !result.value(QStringLiteral("ok")).toBool()) {
                        const QString why = result.value(QStringLiteral("error")).toString(
                            QString::fromUtf8(launch->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1));
                        relay::log::error(QStringLiteral("guest_launch_failed pane=%1 guest=%2 code=%3 error=%4")
                                              .arg(paneLogId(), guest).arg(code).arg(why));
                        status(QStringLiteral("Could not start %1: %2").arg(guestName(guest), why.isEmpty() ? QStringLiteral("the launch helper failed") : why));
                        m_guestWanted.clear();
                        refreshPickers();
                        return;
                    }
                    QString line = result.value(QStringLiteral("command")).toString();
                    // Tier B (26.7): guest_launch reports the session this command line will write
                    // — claude's `--session-id`, or a resumed codex thread — and the next
                    // program_state carries it to the worker.
                    m_guestSession = result.value(QStringLiteral("session_id")).toString();
                    // `cd` and the launch are one command line, so the guest never starts in the
                    // wrong place if the cd fails (26.7).
                    if (!cwd.isEmpty() && QDir::cleanPath(cwd) != QDir::cleanPath(m_cwd))
                        line = QStringLiteral("cd ") + relay::conversations::shellWord(cwd) + QStringLiteral(" && ") + line;
                    const QJsonArray legacy = result.value(QStringLiteral("legacy")).toArray();
                    if (!legacy.isEmpty()) {
                        QStringList files;
                        for (const auto &value : legacy) files << value.toString();
                        notify(guestName(guest), QStringLiteral("Removed Relay's old guest entries from %1 (Relay no longer needs them installed).").arg(files.join(QStringLiteral(", "))));
                    }
                    // Now if the shell is at its prompt, else queued like anything typed while the
                    // terminal is busy; a new pane's shell is not up yet and the queue waits for it.
                    submitTerminal(line, false);
                    status(QStringLiteral("Starting %1…").arg(guestName(guest)));
                    // The detector takes over once the guest is in the foreground; if it never
                    // arrives (the command failed in the shell), the picker stops claiming it.
                    QTimer::singleShot(30000, this, [this, guest] {
                        if (m_guestWanted == guest && m_guest != guest) { m_guestWanted.clear(); refreshPickers(); }
                    });
                });
        connect(launch, &QProcess::errorOccurred, this, [this, launch, guest](QProcess::ProcessError) {
            launch->deleteLater();
            if (m_guestLaunch == launch) m_guestLaunch = nullptr;
            relay::log::error(QStringLiteral("guest_launch_error pane=%1 guest=%2 error=%3").arg(paneLogId(), guest, launch->errorString()));
            status(QStringLiteral("Could not start %1: %2").arg(guestName(guest), launch->errorString()));
            m_guestWanted.clear();
            refreshPickers();
        });
        launch->start();
    }

relay::panestate::Inputs Pane::remoteState() const {
        relay::panestate::Inputs in;
        in.pane = m_token;
        in.busy = m_agentBusy;
        in.toolRunning = m_agentBusy && !m_liveCall.isEmpty();
        const relay::panestatus::Facts facts = statusFacts();
        in.waiting = facts.programAsking || facts.handoffWaiting || facts.questionOpen;
        in.clock = m_agentBusy ? m_turnClockText : QString();
        // The reasoning fold (issue T8CN): its text is the buffer's tail whatever the display mode
        // — the phone draws its own copy from the same bytes — and it is "visible" when the latest
        // fold is on screen. The header says live or last, so a finished trace is never mistaken
        // for a streaming one.
        const QString thinkingUri = !m_thinkingAnchor.isEmpty() ? m_thinkingAnchor : m_lastThinkingAnchor;
        const relay::calllines::Ref thinkingRef = relay::calllines::parseUri(thinkingUri);
        const QString thinkingTurn = thinkingRef.valid ? thinkingRef.turn : QString();
        in.thinkingText = m_turnThinking.value(thinkingTurn).right(relay::panestate::kTailMax);
        in.thinkingVisible = !in.thinkingText.isEmpty() && thinkingDisplay() != QLatin1String("never")
                             && m_backend && m_backend->foldExpanded(thinkingUri);
        in.thinkingHeader = !m_thinkingAnchor.isEmpty()
            ? QStringLiteral("Thinking… · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model)
            : QStringLiteral("Thought · last turn · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model);

        in.queuePaused = queueBlocked();
        in.pauseReason = !m_pauseReason.isEmpty() ? m_pauseReason
                         : queueHeldBySelection() ? QStringLiteral("Held while the next item is edited on the desktop")
                                                  : QString();
        for (const QueueRow &row : queueRows()) {
            if (row.kind == QLatin1String("running")) { in.running = row.preview; in.runningFull = row.full; continue; }
            relay::panestate::Row out{row.id, row.kind, row.preview, row.state, false, row.full};
            for (const QueueEntry &entry : m_entries)
                if (QStringLiteral("entry:%1").arg(entry.id) == row.id) { out.written = entry.written(); break; }
            in.rows << out;
        }
        const bool headSteerable = m_selected == 0 && m_agentBusy && !m_entries.isEmpty() && m_entries.first().agent
                                   && !m_entries.first().written();
        in.queueHint = relay::panestate::queueHint(!m_selectedSteer.isEmpty(), headSteerable, m_selected >= 0);

        // The chip's own text, and the rows of its menu that switch the model, in the menu's
        // own order (owner, 2026-09-19): the roles as "model (role)", then the other presets —
        // the pane's own preset is the main row already. Not the gear (desktop settings) and not
        // the "this turn" image row, which is not a choice.
        // The same strings the desktop box draws (card #MDL1, design 5.3): the chip is the model
        // alone on every mode, and the menu is every class's models, the ones the box draws. A
        // phone is a view of this pane, so it cannot be a different list. The class headers are
        // labels and are not choices; a row of a class this pane is not in says which class it is,
        // because a phone has no headers above it to say so.
        const relay::modelrows::Context phoneRows = modelRowsContext();
        const QString collapsed = relay::modelrows::collapsedText(phoneRows);
        in.modelLabel = !collapsed.isEmpty() ? collapsed
                        : m_modelBox        ? m_modelBox->displayText()
                                            : m_model;
        for (const relay::modelrows::Row &row : relay::modelrows::build(phoneRows)) {
            if (row.header || row.data.startsWith(QStringLiteral("gear:")) || !row.enabled) continue;
            QString text = row.text;
            if (!row.group.isEmpty() && row.group != phoneRows.mode) text += QStringLiteral("  ·  ") + row.group;
            if (!row.trailing.isEmpty()) text += QStringLiteral("  ·  ") + row.trailing;
            in.choices << relay::panestate::Choice{row.data, text,
                                                   row.data == QStringLiteral("role:") + m_agentRole
                                                       || row.data == QStringLiteral("pick:%1|%2")
                                                              .arg(phoneRows.mode, currentEntryKey())};
        }

        in.mode = m_modeValue;
        if (m_editor) in.placeholder = m_editor->placeholderText().isEmpty() ? m_savedPlaceholder : m_editor->placeholderText();

        if (m_ctxLabel && (m_ctxWindow > 0 || !m_ctxGuest.isEmpty())) {
            in.contextLabel = m_ctxLabel->text();
            const double percent = m_ctxNextWindow > 0 ? m_ctxNextPercent : m_ctxPercent;
            if (percent >= 0) in.percentLeft = int(std::lround(std::clamp(100.0 - percent, 0.0, 100.0)));
        }

        // The session manager's rows (/resume, /conversations): agent sessions of this project, as
        // the worker last listed them. Terminal history cannot be resumed and threads belong to
        // their session, so neither is a row here. Nor is a guest session (protocol 26.7):
        // resuming one runs claude or codex in the pane's shell, which is not something a paired
        // device gets to do from a list it is only watching. Observing only: nothing here opens one.
        const QDateTime now = QDateTime::currentDateTime();
        for (const QJsonValue &value : m_remoteSessions) {
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("session_id")).toString();
            const QString source = item.value(QStringLiteral("source")).toString();
            if (id.isEmpty() || source == QLatin1String("terminal") || source == QLatin1String("subagent")
                || relay::conversations::isGuestSource(source)) continue;
            QString title = item.value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) title = item.value(QStringLiteral("first_prompt")).toString();
            if (title.trimmed().isEmpty()) title = QStringLiteral("Untitled conversation");
            const bool current = id == m_sessionId;
            in.sessions << relay::panestate::Session{id, title,
                                                     relay::conversations::whenText(item.value(QStringLiteral("updated")).toDouble(), now),
                                                     current, current && m_agentBusy};
        }
        // The theme the desktop is drawing itself in, by id: the web view has a generated block per
        // shipped theme (app/pane-theme.css) and follows this one (protocol section 16). The
        // application's active theme is the right answer even now that a tab owns a theme, because
        // the tokens, the palette and the stylesheet are the application's — the tab in front
        // decides, and that is the theme this pane is being painted in at this instant. A tab change
        // sets it again, which is a themeChanged() and so a republish.
        // The reasoning level and the levels the model takes (section 3), for the phone's
        // picker: the desktop's own words, empty when the model takes none.
        //
        // Two things a view cannot work out for itself, and used to be left to guess at (#EFT9).
        // **Whether the level is the pane's to change at all**: every other surface gates on
        // `effortFixed` — the desktop's own box is greyed with the reason in its tooltip
        // (refreshSessionControls), so are the model picker and the jobs tab — while this one
        // published a live picker for a level `remoteEffortPick` would refuse. The levels still go
        // (Relay Free's two are worth showing, `Entry::effortFixedReason`), with the fact and the
        // desktop's own sentence beside them, so a view draws the greyed chip the desktop draws.
        // **Which level is the current one**: `m_effort` is the level the pane *carries*, which is
        // not always one this model takes — it snaps on the next refresh, and until then the
        // desktop's box shows `nearestEffort` while the wire said the unsnapped word. A view was
        // then given a list its own `effort` was not in, and ticked nothing. What goes out is the
        // level the desktop's box has selected, which is what section 16 promises a view: the pane
        // as it is drawn here.
        in.efforts = offeredEfforts();
        in.effort = in.efforts.isEmpty() ? m_effort : nearestEffort(in.efforts, m_effort);
        const QString effortWhy = effortFixedReason();
        in.effortFixed = !effortWhy.isEmpty();
        in.effortFixedReason = sentenceCase(effortWhy);   // the greyed box's own tooltip, verbatim
        in.theme = relay::theme::activeThemeId();
        in.canNew = m_workerReady && !m_agentBusy;
        in.canOpen = m_workerReady && !m_agentBusy;   // as the session manager's own rows behave
        return in;
    }

void Pane::printInline(const QString &text, Ink ink) {
        if (text.isEmpty()) return;
        if (!inlineReady()) { m_inlinePending.append({text, ink}); appendTranscript(text, ink); return; }
        endCallRun();   // a held tool-call row ends before anything else prints (#TK9C)
        const QString clean = sanitize(text);
        if (clean.isEmpty()) return;
        if (ink == Ink::Agent) beginBlock(relay::gaps::Block::Agent);
        else if (ink == Ink::User || ink == Ink::UserAgent) beginBlock(relay::gaps::Block::User);
        else if (ink == Ink::Tool) beginBlock(relay::gaps::Block::Call);
        else if (ink == Ink::Recap || ink == Ink::RecapBody) beginBlock(relay::gaps::Block::Recap);   // span, summary, Next ·, Open ·: one block (#5AWD)
        QByteArray out;
        if (!m_inlineOpen) {
            // A remote prompt without Relay's integration cannot be asked to redraw itself: keep
            // its text, and print it back when the block closes (card #S5SH).
            m_login.promptRow.clear();
            m_login.promptBytes.clear();
            if (loginAtPrompt() && !m_login.enhanced) {
                const QPoint cursor = m_backend->cursorPosition();
                // The bytes the host drew the prompt with, so it comes back in its own colours. They
                // are only used when the text in them ends exactly where the cursor is: a prompt drawn
                // with cursor moves (a zsh right-hand prompt) would come back the wrong width, and the
                // screen's own text, padded to the cursor, is the safe answer then.
                int width = 0;
                const QByteArray echo = relay::remote::promptEcho(m_login.line, &width);
                if (cursor.x() > 0 && width == cursor.x()) m_login.promptBytes = echo;
                else if (cursor.y() >= 0)
                    m_login.promptRow = m_backend->screenText().split('\n').value(cursor.y()).left(cursor.x()).leftJustified(cursor.x(), ' ');
            }
            // Erase the idle prompt line; closeInline() asks Readline to redraw it afterwards.
            out += "\r\x1b[2K";
            m_inlineOpen = true; m_atLineStart = true;
            m_wrap.reset();
            holdShellResize(true);
        }
        // Agent prose is Markdown, rendered as it streams (MarkdownAnsi holds back only what it
        // cannot decide yet). Any other ink ends the Markdown run first, so held text lands before it.
        // Everything then goes through the word wrapper, so a line breaks between words at the
        // pane's width rather than wherever the terminal runs out of columns (src/WordWrap.h).
        if (ink == Ink::Agent) {
            // A link's label is clickable because its cells carry this block's anchor with the
            // target as a fragment (#MDKN); the renderer writes that run, so it is told the
            // anchor before it renders the chunk.
            m_markdown.setLinkAnchor(proseUriFor(ink));
            // `![alt](file)` is drawn (#1MGS) — not in a login, where a path names the host's file.
            m_markdown.setInlineImages(!m_login.active);
            m_markdown.setImageBaseDir(m_cwd);
            m_markdown.setImageColumns(m_backend ? m_backend->columns() : 0);
            const QString rendered = m_markdown.feed(clean);
            out += agentProse(rendered);
            m_atLineStart = clean.endsWith('\n');
            writeTerminal(out);
            return;
        }
        m_markdown.setLinkAnchor(proseUriFor(Ink::Agent));   // the tail lands in an Agent block (#MDKN)
        const QString mdTail = m_markdown.finish();   // always runs: it resets the renderer
        if (!mdTail.isEmpty()) out += agentProse(mdTail);
        // A line the user typed carries a *role*, not a colour: every row of it is marked with the
        // private OSC 7772 ("shell" / "agent"), and the engine view paints that row's band and ink
        // from the theme in force when it paints (EngineBackend::applyThemeColors, ColorScheme.h).
        // A colour written here would be frozen in the scrollback; a mark follows a theme switch
        // (owner, 2026-09-19: "can we change the design that the background highlights shift with
        // theme changes"). The text itself is bold in the default foreground.
        if (ink == Ink::User || ink == Ink::UserAgent) {
            const QByteArray mark = ink == Ink::User ? QByteArray("\x1b]7772;shell\x1b\\") : QByteArray("\x1b]7772;agent\x1b\\");
            // The one thing in such a line that is *not* plain: the `/command` it opens with, in
            // the colour the composer already tints it while it is being typed (#SQ3D, and
            // InputHighlighter::highlightAgent). It is written as a palette index, never as an
            // RGB colour — an index is resolved against the theme's own palette when the view
            // paints, so the command follows a theme switch like the rest of the row, and the
            // view moves it as far as the band under it demands (engine/view/FaintInk.h,
            // legibleOn). Bright cyan is where every shipped theme puts this family, and it is
            // the palette entry its `[syntax] token` is drawn from.
            const QPair<int, int> cmd = ink == Ink::UserAgent ? agentSlashSpan(clean) : QPair<int, int>{0, 0};
            QString body = QStringLiteral("\x1b[1m");
            for (int i = 0; i < clean.size(); ++i) {
                if (cmd.second > cmd.first) {
                    if (i == cmd.first) body += QStringLiteral("\x1b[96m");
                    else if (i == cmd.second) body += QStringLiteral("\x1b[39m");
                }
                const QChar ch = clean.at(i);
                if (ch == '\n') body += QStringLiteral("\x1b[0m\n\x1b[1m"); else body += ch;
            }
            if (cmd.second > cmd.first && cmd.second == clean.size()) body += QStringLiteral("\x1b[39m");
            body += QStringLiteral("\x1b[0m");
            // The word wrapper breaks a long line into rows of its own, so the mark goes at the
            // head of each row — never after the last newline, which would hand the role to
            // whatever prints next. A blank row the cursor leaves (it ends in \r\n) is a line
            // break *inside* the block and is marked too, so a multi-paragraph prompt is one
            // solid band rather than banded lines with ground-coloured gaps (#7QFW).
            out += proseStart(ink, body);
            const QByteArray rows = wrapped(body) + terminalLines(m_wrap.flush());
            int from = 0;
            while (from < rows.size()) {
                int end = rows.indexOf("\r\n", from);
                end = end < 0 ? rows.size() : end + 2;
                const QByteArray row = rows.mid(from, end - from);
                const bool empty = QByteArray(row).replace("\x1b[0m", "").replace("\x1b[1m", "").trimmed().isEmpty();
                const bool cursorLeaves = row.endsWith("\r\n");
                out += (empty && !cursorLeaves) ? row : mark + row;
                from = end;
            }
            m_atLineStart = clean.endsWith('\n');
            writeTerminal(out);
            return;
        }
        const QString code = QString::fromUtf8(inkCode(ink));
        QString body = code;
        for (const QChar ch : clean) { if (ch == '\n') body += QStringLiteral("\x1b[0m\n") + code; else body += ch; }
        body += QStringLiteral("\x1b[0m");
        out += proseStart(ink, body);
        out += wrapped(body) + terminalLines(m_wrap.flush());
        m_atLineStart = clean.endsWith('\n');
        writeTerminal(out);
    }

void Pane::rebuildQueueStrip() {
        // Card #XCXD: a resize (showBubble, the lanes stacking) can arrive while the strip is
        // being rebuilt — through the event filter below — and rebuilding again from inside the
        // layout teardown corrupted the heap. Re-entrant calls leave now; the resize branch
        // defers, so the reflow still happens, one event-loop turn later.
        if (m_rebuildingQueueStrip) return;
        m_rebuildingQueueStrip = true;
        struct RebuildDone { bool &flag; ~RebuildDone() { flag = false; } } rebuildDone{m_rebuildingQueueStrip};
        m_paneState.changed();   // pane_state (relay-terminal-71)
        if (!m_queueStrip) return;
        // The worker's rows for this console's surface count towards the strip being there at
        // all: a card's prompt waits in that card's supervisor and never in `m_entries`, and
        // until card #CTRN that meant a queued card prompt had no row on screen (the owner's
        // report on this card). A pane whose context names no surface has none of them.
        const QList<WorkerRow> waiting = workerSteers();
        const QList<WorkerRow> queued = workerQueued();
        const bool workerPaused = m_queuePaused && !queued.isEmpty() && !queueSurface().isEmpty();
        // Card #XCXD: a running shell keeps the strip (and its stop control) up even when
        // nothing is pending — a stop must not depend on there being a queue to show. A busy
        // agent alone does not: the Relaying line under the transcript already says it runs and
        // that Esc stops it, and a strip repeating that was the owner's complaint.
        const bool visible = !m_entries.isEmpty() || m_entriesPaused || !m_steering.isEmpty()
                          || !waiting.isEmpty() || !queued.isEmpty()
                          || (processBusy() && m_backend);
        auto *layout = static_cast<QVBoxLayout *>(m_queueStrip->layout());
        // Card #XCXD: the lanes nest — a header row, then either one list or two columns that
        // each carry their own header. Clear every level, keeping only the two list widgets
        // (they are re-added below); anything else would leak or be reparented onto a deleted
        // layout on the next rebuild.
        std::function<void(QLayout *)> clearStripLayout = [&](QLayout *l) {
            while (QLayoutItem *item = l->takeAt(0)) {
                if (QWidget *w = item->widget()) { if (w != m_queueList && w != m_terminalQueueList) w->deleteLater(); }
                else if (QLayout *sub = item->layout()) clearStripLayout(sub);
                delete item;
            }
        };
        clearStripLayout(layout);
        m_queueWanted = visible;
        if (visible) showBubble(m_queueStrip); else hideBubble(m_queueStrip);
        if (!visible) return;
        auto *header = new QHBoxLayout;
        const bool held = queueHeldBySelection();
        auto *title = new QLabel(QStringLiteral("QUEUE"));   // each lane says its own state
        title->setObjectName(QStringLiteral("queueTitle"));
        QString why = held ? QStringLiteral("The next item is highlighted and being edited in the prompt box, so the"
                                            " queue is holding. Enter saves it and the queue runs on; Esc keeps the"
                                            " text and stops a resource instead.")
                           : QString();
        if (!m_pauseReason.isEmpty()) why = why.isEmpty() ? m_pauseReason : why + QStringLiteral("\nAlso paused: ") + m_pauseReason;
        title->setToolTip(why);
        header->addWidget(title, 1);
        const bool headSteerable = selectedQueueRow() == steerRowCount() && m_agentBusy
                                   && (!queued.isEmpty()
                                       || (!m_entries.isEmpty() && m_entries.first().agent
                                           && !m_entries.first().written()));
        // Paused by a Stop or a failed turn, with no row holding the queue instead: the two ways
        // back are the Resume button and Enter on the empty prompt box, and the hint says the
        // second one wherever the button is offered (#7JD1).
        const bool paused = (m_entriesPaused || m_agentPaused || workerPaused) && !held;
        auto *hint = new QLabel(!m_selectedSteer.isEmpty()
                                    ? QStringLiteral("Enter or type to edit · Ctrl+↓ back to the queue · Ctrl+Enter now · Shift+Del withdraw")
                                : headSteerable
                                    ? QStringLiteral("Ctrl+↑ next tool call · Ctrl+↓ move · Enter save · Down off the end leaves · Shift+Del remove")
                                : inQueueSelection()
                                    ? QStringLiteral("↑↓ row · Ctrl+↑↓ move · Enter save · Down off the end leaves · Shift+Del remove")
                                : paused
                                    ? QStringLiteral("Enter resumes · ↑ take back to edit · × remove")
                                    : QStringLiteral("↑ take back to edit · drag to reorder · × remove"));
        hint->setObjectName(QStringLiteral("queueHint"));
        hint->setToolTip(QStringLiteral(
            "Rows run top to bottom. ↪ rows reach the agent inside this turn, at its next tool call; the rest run after it.\n"
            "↑ on the empty prompt box removes the top editable row and restores it as an unsent draft.\n"
            "Enter submits that draft again; the remaining queued rows keep their order.\n"
            "Drag queued rows to reorder; × removes a row. A ↪ row is withdrawn if the agent has not taken it yet.\n"
            "Agent and Terminal are separate queues: a row only ever moves within its own.\n"
            "Rows without editable text stay selected: Ctrl+↑↓ moves them, Shift+Delete removes; Enter saves a row.\n"
            "A queue a Stop paused runs again on Enter in the empty prompt box, or on the Resume button;"
            " typing a prompt and sending it resumes the rows behind it too."));
        header->addWidget(hint);
        // Card #XCXD: the shell keeps its own stop control while it runs, with nothing queued
        // too. The agent's is the Relaying line's "Esc stops", so the strip does not repeat it.
        // The label shows the live keymap binding when there is one and the card's contextual
        // key otherwise: Esc when the shell is all that runs, Alt+Esc when the agent runs too.
        const bool agentRunning = m_agentBusy;
        const bool shellRunning = processBusy() && m_backend;
        auto liveKey = [](const char *action, const QString &fallback) {
            const QString bound = Keymap::instance().shortcutText(QString::fromLatin1(action));
            return bound.isEmpty() ? fallback : bound;
        };
        if (shellRunning) {
            auto *stop = new QToolButton;
            const QString shellKey = (agentRunning || m_altScreen)
                ? liveKey("terminal.interrupt", QStringLiteral("Alt+Esc"))
                : QStringLiteral("Esc");   // a shell command alone: Esc is its key, binding or no binding
            stop->setText(QStringLiteral("Stop shell (%1)").arg(shellKey));
            stop->setFocusPolicy(Qt::NoFocus);
            stop->setToolTip(QStringLiteral("Interrupt the running command · Alt+Esc does it from the keyboard"));
            connect(stop, &QToolButton::clicked, this, [this] { forceInterruptShell(); });
            header->addWidget(stop);
        }
        layout->addLayout(header);
        const QString running = runningLabel();
        // Card #JDN4: the strip is at most half the pane (bubbleSpan() / 2), so what an open line
        // may take is shared out of that: the running line up to a quarter, the lanes the rest.
        const int stripBudget = std::max(bubbleRow(), bubbleSpan() / 2);
        int runningUsed = 0;
        if (!running.trimmed().isEmpty()) {
            // Card #JDN4: ▾ opens the running line to the whole message, wrapped in place; ▴ folds
            // it. A new running item starts folded.
            if (running != m_queueRunningExpandedFor) { m_queueRunningExpandedFor = running; m_queueRunningExpanded = false; }
            const QString whole = running.trimmed();
            const int room = std::max(160, width() - 180);
            const bool more = m_queueRunningExpanded || whole.contains(QLatin1Char('\n'))
                           || fontMetrics().horizontalAdvance(whole.simplified()) > room;
            const bool open = more && m_queueRunningExpanded;
            auto *line = new QHBoxLayout;
            line->setContentsMargins(0, 0, 0, 0);
            auto *label = new QLabel;
            label->setObjectName(QStringLiteral("queueRunning"));
            label->setTextFormat(Qt::PlainText);
            if (open) {
                label->setText(QStringLiteral("▸ running  ") + whole);
                label->setWordWrap(true);
                label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
                label->setTextInteractionFlags(Qt::TextSelectableByMouse);
                // Past its share of the strip it scrolls there rather than pushing the lanes away.
                auto *scroll = new QScrollArea;
                scroll->setObjectName(QStringLiteral("queueRunningScroll"));
                scroll->setFrameShape(QFrame::NoFrame);
                scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                scroll->setWidgetResizable(true);
                scroll->setWidget(label);
                const int textWidth = std::max(160, width() - 60);
                const int cap = std::clamp(stripBudget / 4, 3 * fontMetrics().lineSpacing(), 12 * fontMetrics().lineSpacing());
                scroll->setFixedHeight(std::min(label->heightForWidth(textWidth), cap) + 4);
                runningUsed = scroll->height();
                line->addWidget(scroll, 1);
            } else {
                label->setText(QStringLiteral("▸ running  ") + fontMetrics().elidedText(whole.simplified(), Qt::ElideRight, room - (more ? 30 : 0)));
                line->addWidget(label, 1);
            }
            if (more) {
                auto *toggle = new QToolButton;
                toggle->setObjectName(QStringLiteral("queueRunningExpand"));
                toggle->setAutoRaise(true);
                toggle->setFocusPolicy(Qt::NoFocus);
                toggle->setText(open ? QStringLiteral("▴") : QStringLiteral("▾"));
                toggle->setToolTip(open ? QStringLiteral("Show one line") : QStringLiteral("Show the whole message"));
                connect(toggle, &QToolButton::clicked, this, [this] {
                    m_queueRunningExpanded = !m_queueRunningExpanded;
                    QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
                });
                line->addWidget(toggle, 0, Qt::AlignTop);
            }
            layout->addLayout(line);
        }
        m_queueStrip->installEventFilter(this);
        if (!m_queueList) {
            m_queueList = new QListWidget(m_queueStrip);
            m_queueList->setObjectName(QStringLiteral("queueList"));
            m_queueList->setItemDelegate(new QueueRowDelegate(m_queueList));
            m_queueList->setFocusPolicy(Qt::NoFocus);
            m_queueList->setFrameShape(QFrame::NoFrame);
            m_queueList->setSelectionMode(QAbstractItemView::SingleSelection);
            m_queueList->setDragDropMode(QAbstractItemView::InternalMove);
            m_queueList->setDefaultDropAction(Qt::MoveAction);
            m_queueList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_queueList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);   // an open row is tall (#JDN4)
            m_queueList->setResizeMode(QListView::Adjust);
            connect(m_queueList->model(), &QAbstractItemModel::rowsMoved, this, [this] { syncEntriesFromList(); });
            connect(m_queueList->model(), &QAbstractItemModel::rowsInserted, this, [this] { if (!m_fillingQueueList) syncEntriesFromList(); });
        }
        // Card #XCXD: the terminal lane's own list. Dragging reorders it within itself — a row
        // cannot cross resources because the other lane's list is a different widget — and the
        // drag persists through syncEntriesFromList, which writes each lane back into its own
        // slots.
        if (!m_terminalQueueList) {
            m_terminalQueueList = new QListWidget(m_queueStrip);
            m_terminalQueueList->setObjectName(QStringLiteral("terminalQueueList"));
            m_terminalQueueList->setItemDelegate(new QueueRowDelegate(m_terminalQueueList));
            m_terminalQueueList->setFocusPolicy(Qt::NoFocus);
            m_terminalQueueList->setFrameShape(QFrame::NoFrame);
            m_terminalQueueList->setSelectionMode(QAbstractItemView::SingleSelection);
            m_terminalQueueList->setDragDropMode(QAbstractItemView::InternalMove);
            m_terminalQueueList->setDefaultDropAction(Qt::MoveAction);
            m_terminalQueueList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_terminalQueueList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);   // an open row is tall (#JDN4)
            m_terminalQueueList->setResizeMode(QListView::Adjust);
            connect(m_terminalQueueList->model(), &QAbstractItemModel::rowsMoved, this, [this] { syncEntriesFromList(); });
            connect(m_terminalQueueList->model(), &QAbstractItemModel::rowsInserted, this, [this] { if (!m_fillingQueueList) syncEntriesFromList(); });
            // Clicking a terminal row picks the same composite row the keyboard lands on, so
            // the pane's queue editing works on it without a second selection scheme.
            connect(m_terminalQueueList, &QListWidget::itemClicked, this, [this](QListWidgetItem *clicked) {
                const qulonglong id = clicked->data(QueueRowDelegate::EntryIdRole).toULongLong();
                int entry = -1;
                for (int i = 0; i < m_entries.size(); ++i)
                    if (m_entries.at(i).id == id) { entry = i; break; }
                if (entry < 0) return;
                int agentCount = 0, lanePos = 0;
                for (int i = 0; i < m_entries.size(); ++i) if (m_entries.at(i).agent) ++agentCount;
                const bool agentLane = m_entries.at(entry).agent;
                for (int i = 0; i < entry; ++i) if (m_entries.at(i).agent == agentLane) ++lanePos;
                selectQueueRow(steerRowCount() + int(workerSteers().size()) + int(workerQueued().size())
                             + (agentLane ? lanePos : agentCount + lanePos) + 1);
            });
        }
        m_fillingQueueList = true;
        m_queueList->clear();
        m_terminalQueueList->clear();
        using Row = QueueRowDelegate;
        QListWidgetItem *current = nullptr, *terminalCurrent = nullptr;
        // Steers first: they reach the agent before anything queued. Not draggable, because "the
        // next tool call" is not a place among the others; Ctrl+↓ is how one goes back.
        for (const auto &steer : std::as_const(m_steering)) {
            auto *item = new QListWidgetItem(steer.text, m_queueList);
            item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(0));
            item->setData(Row::AgentRole, true);
            item->setData(Row::KindRole, QStringLiteral("steer"));
            item->setData(Row::RowIdRole, QStringLiteral("steer:") + steer.requestId);
            item->setData(Row::PendingRole, steer.withdraw);
            item->setData(Row::SendNowRole, !steer.withdraw && m_agentBusy && !m_ask.open());
            item->setToolTip(steer.withdraw
                ? QStringLiteral("Withdrawing · unless the agent reaches its next tool call first")
                : QStringLiteral("Delivered inside the running turn at the agent's next tool call\n%1\n\n"
                                 "Enter or typing takes it back to edit · Ctrl+↓ back to the queue · Ctrl+Enter sends it now"
                                 " · Shift+Delete or × withdraws it").arg(steer.text));
            item->setFlags(steer.withdraw ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (steer.requestId == m_selectedSteer) current = item;
        }
        // A `/model` steered into the turn (card #7QH0): drawn as a steer — it lands at the next
        // tool call — with its own ↻ glyph. Not selectable or draggable; × withdraws it, → (or
        // the second Enter) interrupts and switches now.
        if (m_modelSteer) {
            const bool pending = m_modelSteer->withdrawing || m_modelSteer->interrupting;
            auto *item = new QListWidgetItem(QStringLiteral("/model ") + m_modelSteer->name, m_queueList);
            item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(0));
            item->setData(Row::AgentRole, true);
            item->setData(Row::KindRole, QStringLiteral("steer"));
            item->setData(Row::ModelRole, true);
            item->setData(Row::RowIdRole, QString::fromLatin1(kModelSteerRow));
            item->setData(Row::PendingRole, pending);
            item->setData(Row::PendingTextRole, m_modelSteer->interrupting ? QStringLiteral("  switching now…") : QString());
            item->setData(Row::SendNowRole, !pending && m_agentBusy && !m_ask.open());
            item->setToolTip(m_modelSteer->interrupting
                ? QStringLiteral("Interrupting the turn · %1 takes over as it stops").arg(m_modelSteer->name)
                : m_modelSteer->withdrawing
                ? QStringLiteral("Withdrawing · unless the agent reaches its next tool call first")
                : QStringLiteral("%1 takes over at the agent's next tool call · the request answering now finishes first\n\n"
                                 "Enter again or → interrupts and switches now · × withdraws it").arg(m_modelSteer->name));
            item->setFlags(pending ? Qt::NoItemFlags : Qt::ItemIsEnabled);
        }
        // Then the worker's own rows for this surface, in its order: the steers it holds inside
        // the running turn, then its queue. They carry no `EntryIdRole` and are not draggable —
        // `syncEntriesFromList` reorders `m_entries`, and a row that is not in that list has no
        // place in a drop — so Ctrl+↑↓, which sends `queue_move`, is how one is reordered.
        for (const QList<WorkerRow> *group : {&waiting, &queued}) {
            const bool steering = group == &waiting;
            for (const WorkerRow &row : *group) {
                auto *item = new QListWidgetItem(row.preview, m_queueList);
                item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(0));
                item->setData(Row::AgentRole, true);
                item->setData(Row::KindRole, steering ? QStringLiteral("steer") : QStringLiteral("agent"));
                item->setData(Row::RowIdRole, QStringLiteral("item:") + row.id);
                const QString what = row.mode.isEmpty()
                                         ? QStringLiteral("Queued on the agent")
                                         : QStringLiteral("Queued to %1 #%2").arg(row.mode, row.cardId);
                item->setToolTip(steering
                    ? QStringLiteral("Delivered inside the running turn at the agent's next tool call\n%1").arg(row.preview)
                    : workerRowText(row.id).isEmpty()
                        ? QStringLiteral("%1\n%2\n\nSent from elsewhere, so this row shows what the "
                                         "worker kept of it: Ctrl+↑↓ moves it, Shift+Delete or × removes it.")
                              .arg(what, row.preview)
                        : QStringLiteral("%1\n%2\n\nEnter or typing edits it · Ctrl+↑↓ moves it · "
                                         "Shift+Delete or × removes it").arg(what, row.preview));
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (row.id == m_selectedWorkerRow) current = item;
            }
        }
        // Card #XCXD: two lanes, one FIFO per resource. The agent's rows (and every steer) stay
        // in the main list; the terminal's rows go to their own list beside it. The grouping here
        // is the order selectQueueRow maps rows through, so the keyboard still walks them.
        auto fillLane = [this, &current, &terminalCurrent](bool agentLane) {
            for (int i = 0; i < m_entries.size(); ++i) {
                const QueueEntry &entry = m_entries[i];
                if (entry.agent != agentLane) continue;
                auto *item = new QListWidgetItem(entry.label(), agentLane ? m_queueList : m_terminalQueueList);
                item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(entry.id));
                item->setData(Row::AgentRole, entry.agent);
                item->setData(Row::KindRole, entry.agent ? QStringLiteral("agent") : QStringLiteral("command"));
                item->setData(Row::ModelRole, entry.isModel());
                item->setData(Row::RowIdRole, QStringLiteral("entry:%1").arg(entry.id));
                item->setData(Row::SendNowRole, entry.agent && !entry.written() && entry.guest.isEmpty()
                                              && m_configured && !m_ask.open());
                item->setToolTip(entry.isModel()
                    ? QStringLiteral("Switch to %1 when its turn comes · the running turn finishes on %2\n\n"
                                     "Enter on the empty prompt box switches at the next tool call, twice interrupts and switches now"
                                     " · → switches now · × drops it · picking again replaces it")
                          .arg(entry.modelName, modelNameFor(m_currentPreset, m_model))
                    : entry.text);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
                if (i == m_selected) { if (agentLane) current = item; else terminalCurrent = item; }
            }
        };
        fillLane(true);
        fillLane(false);
        // Card #JDN4: the rows opened to their whole text, re-applied to the new items; a row
        // that has left the queue leaves the set. A worker row's whole text is what the worker
        // kept of it, not its 120-character preview.
        QSet<QString> stillOpen;
        for (QListWidget *lane : {m_queueList, m_terminalQueueList})
            for (int i = 0; i < lane->count(); ++i) {
                QListWidgetItem *item = lane->item(i);
                const QString rowId = item->data(Row::RowIdRole).toString();
                if (rowId.startsWith(QStringLiteral("item:")))
                    if (const QString text = workerRowText(rowId.mid(5)); !text.isEmpty()) item->setData(Row::FullTextRole, text);
                if (m_expandedQueueRows.contains(rowId)) { item->setData(Row::ExpandedRole, true); stillOpen.insert(rowId); }
            }
        m_expandedQueueRows = stillOpen;
        m_fillingQueueList = false;
        if (current) m_queueList->setCurrentItem(current);
        else { m_queueList->clearSelection(); m_queueList->setCurrentRow(-1); }
        if (terminalCurrent) m_terminalQueueList->setCurrentItem(terminalCurrent);
        else { m_terminalQueueList->clearSelection(); m_terminalQueueList->setCurrentRow(-1); }
        const int rowHeight = std::max(20, fontMetrics().height() + 8);
        const int agentRows = int(m_steering.size()) + (m_modelSteer ? 1 : 0) + int(waiting.size()) + int(queued.size())
                          + int(std::count_if(m_entries.cbegin(), m_entries.cend(), std::mem_fn(&QueueEntry::agent)));
        const int terminalRows = int(m_entries.size()) - int(std::count_if(m_entries.cbegin(), m_entries.cend(), std::mem_fn(&QueueEntry::agent)));
        const int rows = agentRows + terminalRows;
        // Six rows' worth, as before; a lane with an open row may take what the strip has left
        // (card #JDN4), and scrolls by pixel past that.
        const int openCap = std::max(3 * rowHeight, stripBudget - runningUsed - 4 * rowHeight);
        auto laneHeight = [rowHeight, openCap](QListWidget *lane, int laneRows) {
            int open = 0, total = 0;
            for (int i = 0; i < lane->count(); ++i) {
                const bool expanded = lane->item(i)->data(Row::ExpandedRole).toBool();
                open += expanded ? 1 : 0;
                total += expanded ? std::max(rowHeight, lane->sizeHintForRow(i)) : rowHeight;
            }
            if (!open) return std::min<int>(6, std::max<int>(1, laneRows)) * rowHeight + 4;
            return std::min(total, openCap) + 4;
        };
        m_queueList->setFixedHeight(laneHeight(m_queueList, agentRows));
        m_terminalQueueList->setFixedHeight(laneHeight(m_terminalQueueList, terminalRows));
        m_queueList->setVisible(agentRows > 0);
        m_terminalQueueList->setVisible(terminalRows > 0);
        if (current) m_queueList->scrollToItem(current);
        if (terminalCurrent) m_terminalQueueList->scrollToItem(terminalCurrent);
        // Card #XCXD: each lane owns its state — Agent (prompts and steers, the worker's queue
        // paused counts here) and Terminal (commands). One resource pausing must not read as the
        // other's queue stopping, and the strip header no longer offers a Resume or a Clear that
        // would blur them. The agent lane's Resume is resumeAgentQueue(), which resumes the
        // worker's queue too; the terminal lane's is its own. Owner, 2026-09-24: no "Agent · N" /
        // "Terminal · N" labels — which lane is which is obvious from the rows, so a header only
        // exists when it has something to do: a Resume, or the agent lane's Clear.
        auto laneHeader = [this, queued](bool agentLane, int rows, bool lanePaused) {
            auto *head = new QHBoxLayout;
            if (lanePaused) {
                auto *label = new QLabel(QStringLiteral("paused"));
                label->setObjectName(agentLane ? QStringLiteral("agentLaneLabel") : QStringLiteral("terminalLaneLabel"));
                head->addWidget(label);
            }
            head->addStretch(1);
            if (lanePaused) {
                auto *resume = new QToolButton; resume->setText(QStringLiteral("Resume")); resume->setFocusPolicy(Qt::NoFocus);
                resume->setObjectName(agentLane ? QStringLiteral("resumeAgentQueue") : QStringLiteral("resumeTerminalQueue"));
                resume->setToolTip(agentLane ? QStringLiteral("Run the agent's queue again · Enter on the empty prompt box does the same")
                                             : QStringLiteral("Run the terminal's queue again · the agent lane is not touched"));
                connect(resume, &QToolButton::clicked, this, [this, agentLane] {
                    if (agentLane) resumeAgentQueue();
                    else { m_entriesPaused = false; m_pauseReason.clear(); rebuildQueueStrip(); changed(); pumpQueue(); }
                });
                head->addWidget(resume);
            }
            if (agentLane && int(m_entries.size()) + queued.size() > 1) {
                auto *clear = new QToolButton; clear->setText(QStringLiteral("Clear")); clear->setFocusPolicy(Qt::NoFocus);
                clear->setObjectName(QStringLiteral("clearAgentQueue"));
                clear->setToolTip(QStringLiteral("Clear the agent's queue"));
                connect(clear, &QToolButton::clicked, this, [this] { clearAgentQueue(); });
                head->addWidget(clear);
            }
            return head;
        };
        const bool agentLanePaused = (agentQueuePaused() || workerPaused) && !held;
        const bool agentLaneHasClear = int(m_entries.size()) + queued.size() > 1;
        const bool agentHead = agentLanePaused || agentLaneHasClear;
        const bool terminalHead = m_entriesPaused;
        const bool dual = agentRows > 0 && terminalRows > 0;
        if (dual) {
            const bool stacked = m_queueStrip->width() < 640;
            m_queueLanesStacked = stacked;
            if (stacked) {
                if (agentHead) layout->addLayout(laneHeader(true, agentRows, agentLanePaused));
                layout->addWidget(m_queueList);
                if (terminalHead) layout->addLayout(laneHeader(false, terminalRows, m_entriesPaused));
                layout->addWidget(m_terminalQueueList);
            } else {
                auto *side = new QHBoxLayout;
                auto *left = new QVBoxLayout;
                if (agentHead) left->addLayout(laneHeader(true, agentRows, agentLanePaused));
                left->addWidget(m_queueList);
                auto *right = new QVBoxLayout;
                if (terminalHead) right->addLayout(laneHeader(false, terminalRows, m_entriesPaused));
                right->addWidget(m_terminalQueueList);
                side->addLayout(left, 1);
                side->addLayout(right, 1);
                layout->addLayout(side);
            }
        } else if (agentRows > 0) {
            m_queueLanesStacked = false;
            if (agentHead) layout->addLayout(laneHeader(true, agentRows, agentLanePaused));
            layout->addWidget(m_queueList);
        } else if (terminalRows > 0 || m_entriesPaused) {
            // Only a lane with rows (or a paused one, for its Resume) gets a header: a running
            // shell with nothing waiting shows its stop button and no lane furniture at all.
            m_queueLanesStacked = false;
            if (terminalHead) layout->addLayout(laneHeader(false, terminalRows, m_entriesPaused));
            layout->addWidget(m_terminalQueueList);
        }
        placeQueueStrip();
        QTimer::singleShot(0, this, [this] { placeQueueStrip(); });
    }

void Pane::pollShell() {
        tunePoll();
        if (m_shellResizeHeld && !shellIdleAtPrompt()) holdShellResize(false);
        // PROMPT_COMMAND runs before Readline puts the tty into noncanonical mode.
        // Recheck on every tick, even when the state file has not changed.
        refreshShellReady();
        if (!m_entries.isEmpty() && !m_activeValid) pumpQueue();
        // The guest channel is polled on the same tick (26.3), before state.json's own checks
        // below can return: a guest event must land even in a tick where the shell did not.
        pollGuestEvents();
        // This runs 12 times a second in every pane, and the file changes a few times per
        // command. shell/event.py replaces it atomically, so a new event is a new inode: one
        // stat() says whether there is anything to read, in place of an open, a read and a JSON
        // parse. A small saving; tunePoll() above is the larger one.
        const QString statePath = m_runtime.filePath(QStringLiteral("state.json"));
#ifndef Q_OS_WIN
        struct stat info;
        if (::stat(QFile::encodeName(statePath).constData(), &info) != 0) return;
#ifdef Q_OS_MACOS
        const timespec modified = info.st_mtimespec;
#else
        const timespec modified = info.st_mtim;
#endif
        if (m_stateSeen && info.st_ino == m_stateInode && info.st_size == m_stateSize
            && modified.tv_sec == m_stateMtime.tv_sec && modified.tv_nsec == m_stateMtime.tv_nsec) return;
#endif
        QFile file(statePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
#ifndef Q_OS_WIN
        m_stateSeen = true; m_stateInode = info.st_ino; m_stateSize = info.st_size; m_stateMtime = modified;
#endif
        const auto event = QJsonDocument::fromJson(file.readAll()).object();
        file.close(); // Allow Windows shell events to atomically replace state.json.
        if (event.value(QStringLiteral("token")).toString() != m_token) return;
        const auto sequence = event.value(QStringLiteral("sequence")).toString();
        if (sequence.isEmpty() || sequence == m_shellSequence) return;
        m_shellSequence = sequence; m_seenShell = true;
        const QString stage = event.value(QStringLiteral("event")).toString();
        if (const int reported = event.value(QStringLiteral("shell_pid")).toInt(); reported > 0) {
            const bool fresh = m_shellPid != reported;
            m_shellPid = reported;
            // The shell naming itself is the last thing the bridge's router needs (26.5); a pane
            // that registered before its shell existed says so now, before the prompt a claude
            // could be started at.
            if (fresh) registerWithBridge();
        }
        const QString newCwd = event.value(QStringLiteral("cwd")).toString(m_cwd);
        if (newCwd != m_cwd) {
            m_cwd = newCwd; updatePaths(); changed();
            // The bridge routes by the longest workspace/cwd prefix (26.5): a pane that moved must
            // say so before a request names a place it is no longer in.
            registerWithBridge();
        }
        if (stage == QStringLiteral("ready")) {
            // The first of the shell's prompts is the one a restored pane has been waiting for
            // (#XQ8F: the saved remote_login line re-attaches below, beside the scrollback).
            const bool firstPrompt = !m_promptReported;
            m_promptReported = true;
            refreshShellReady();
            m_knownCommands = event.value(QStringLiteral("known_commands")).toArray();
            if (m_highlighter) m_highlighter->setKnownCommands(knownCommandNames());
            m_shellPath = event.value(QStringLiteral("path")).toString();
            const int status = event.value(QStringLiteral("status")).toInt();
            if (!m_agentBusy) this->status(QStringLiteral("Shell ready · exit %1").arg(status));
            // A restored pane brings its old text back above this first prompt.
            replayRestoredScrollback();
            // And its remote_login line follows at once, unlike the restored queue above it: the
            // holder session it re-attaches to is the whole point of staying alive (#XQ8F).
            if (firstPrompt) runRestoredRemoteLogin();
            // Fast commands can finish between two polls, so "running" is not a reliable trigger.
            const bool suggestNext = m_commandLoaded;
            m_commandLoaded = false;
            if (suggestNext && !m_pendingCommand.isEmpty() && !m_agentBusy
                && QSettings().value(QStringLiteral("suggestions/next_command"), true).toBool()) {
                const QString command = m_pendingCommand;
                QTimer::singleShot(250, this, [this, command, status] {
                    requestSuggestion(QStringLiteral("next_command"), {{"command", command}, {"exit_status", status}, {"cwd", m_cwd}});
                });
            }
            if (m_runningSince.isValid()) {
                const qint64 ms = m_runningSince.elapsed();
                m_runningSince.invalidate();
                if (ms > 30000) notify(QStringLiteral("Command finished"), QStringLiteral("Exit %1 after %2 s in %3").arg(status).arg(ms / 1000).arg(m_cwd),
                                       status == 0 ? relay::NotificationCenter::kindSuccess : relay::NotificationCenter::kindWarning);
            }
            m_programPoll.stop();
            endWaiting(true);
            updateOpaqueProgram();
            // The program is gone: the screen has nothing to ask and the agent's permission to
            // type into it ends with it (cards YR21, C1HH). The guest agent goes with it (GT7X).
            endDelegation(QStringLiteral("program_exited"));
            setGuest({});
            updateScreenPrompt();
            m_remoteHandled = false; m_remoteProgram = false;
            if (m_login.active) finishLoginCommand(status);
            endLogin();
            m_secretDeclined = false; m_secretNotified = false;
            leaveSecretMode();
            if (m_native && m_autoHuman) { m_autoHuman = false; setNative(false, false); }
            updateTakeControl();
            const bool activeFinished = m_activeValid && !m_active.agent && m_activeLoaded;
            if (activeFinished) {
                // The queued command finished; a failure pauses whatever is queued behind it.
                m_activeValid = false; m_activeLoaded = false;
                if (status != 0) pauseQueue(QStringLiteral("`%1` exited with status %2").arg(m_active.text).arg(status));
            }
            // The strip came up with the program ("running" above) whether or not the queue ran
            // it, so it goes down the same way: a command typed straight at the prompt is no
            // queue entry, but its Stop-shell control must not outlive it.
            QTimer::singleShot(150, this, [this, activeFinished] { rebuildQueueStrip(); if (activeFinished) pumpQueue(); });
            if (m_fixArmed) {
                const QString command = m_fixCommand;
                const int attempt = m_fixAttempt;
                m_fixArmed = false; m_fixWatch = false;
                if (status == 0) {
                    if (attempt > 0) { printInline(QStringLiteral("✓ Fixed command succeeded.\n"), Ink::Note); closeInline(); }
                    clearFix();
                } else if (status == 130) {
                    clearFix();  // Interrupted with Ctrl+C: the user stopped it on purpose.
                } else {
                    // Wrong-mode hints: the command ran and failed but read like a request, so the
                    // pane is probably in the wrong input mode. The fix attempt continues regardless.
                    if (m_commandNatural && m_modeValue == QStringLiteral("shell")) wrongModeHint(true);
                    // Defer until Readline has drawn the prompt and put the tty in raw mode.
                    QTimer::singleShot(200, this, [this, command, attempt, status] {
                        startFix(command, QStringLiteral("exited with status %1").arg(status), attempt + 1);
                    });
                }
            }
            m_commandNatural = false;   // consumed by this completion either way
            // The command Relay ran has finished: index its line, exit status and captured output.
            finishCommandCapture(status);
            if (m_handoffArmed) finishHandoff(status);
            QTimer::singleShot(120, this, [this] { flushInline(); });
        } else if (stage == QStringLiteral("running")) {
            m_shellReady = false; m_promptReported = false;
            m_runningSince.start(); m_secretNotified = false;
            // The prompt box stays visible while ordinary programs run so more commands and prompts
            // can be queued. It hides for the alternate screen (Session signal), password prompts,
            // and remote sessions; a program blocked reading the terminal gets the focus instead.
            m_waitTicks = 0; m_echoTicks = 0; m_remoteHandled = false; m_remoteProgram = false; endLogin(); m_secretDeclined = false;
            m_programPoll.start();
            maybeAutoDelegate();   // card #H2KQ: the agent always drives
            QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
        } else if (stage == QStringLiteral("loaded") && m_loading && m_backend &&
                   event.value(QStringLiteral("input_sha256")).toString() == m_pendingHash) {
            m_loading = false; m_shellReady = false; m_promptReported = false; m_refocus = true;
            m_editor->remember(m_pendingCommand);
            QString historyError;
            if (!m_commandSuggestions.remember(m_pendingCommand, &historyError))
                status(QStringLiteral("Could not save command suggestions: %1").arg(historyError));
            m_commandLoaded = true;
            m_commandLog.append({m_pendingCommand, m_cwd});
            if (m_commandLog.size() > 500) m_commandLog.removeFirst();
            // run_in_terminal (protocol 22): this is the command the agent handed over.
            m_handoffArmed = m_handoffNext; m_handoffNext = false;
            if (m_handoffArmed) m_handoffCommand = m_pendingCommand;
            answerTerminalCommand(true, QStringLiteral("started"));
            beginCommandCapture(m_pendingCommand, m_handoffArmed);   // conversation index (protocol 14)
            const bool fromQueue = m_activeValid && !m_active.agent;
            if (fromQueue) m_activeLoaded = true;
            // Do not discard edits typed while waiting for the shell acknowledgement.
            else if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
            m_fixArmed = m_fixWatch;
            // Focus stays in the prompt box so more commands and prompts can be queued.
            sendShellInput(QStringLiteral("\r"));
        } else if (stage == QStringLiteral("unsupported")) {
            m_shellReady = false; m_promptReported = false; setNative(true);
            status(QStringLiteral("Your shell already has a DEBUG hook. It was left untouched; use native mode or relaunch with --clean-shell."));
        }
    }

// The holder-session runtime of card #XQ8F. The declarations and members are Pane.h's; the
// bodies live here, beside the shell environment that arms the wrapper they belong to.

// A pane whose layout leaf carried a remote_login line re-attaches to its holder session
// (docs/SSH-AND-MOSH.md section 3b): the holder kept running on the host when Relay was last
// quit, and surviving that is the feature — so unlike the restored queue, which waits for the
// person to resume it, the line runs once, at the pane's own shell's first prompt, without
// asking. A pane that started a program instead of a shell never reports that prompt (only the
// shell bridge writes the state pollShell reads here), and the flag keeps a shell restarted
// later from running the line a second time into whatever the person is doing by then.
void Pane::runRestoredRemoteLogin() {
    // The local holder's line (#87HB) runs through the same door: the pane's own shell is at
    // its first prompt, the restored scrollback and queue are already in place, and the line's
    // holder attaches the pane to the session that outlived the quit.
    if (m_remoteLoginRestore.isEmpty() && !m_localLoginRestore.isEmpty() && !m_remoteLoginRan) {
        m_remoteLoginRan = true;   // one restored line per pane, remote or local
        const QString localLine = m_localLoginRestore;
        m_localLoginRestore.clear();
        relay::log::info(QStringLiteral("local_login_restored pane=%1 line=%2")
                             .arg(paneLogId(), localLine));
        queueCommand(localLine);
        return;
    }
    if (m_remoteLoginRestore.isEmpty() || m_remoteLoginRan) return;
    m_remoteLoginRan = true;
    relay::log::info(QStringLiteral("remote_login_restored pane=%1 line=%2")
                         .arg(paneLogId(), m_remoteLoginRestore));
    queueCommand(m_remoteLoginRestore);
}

// The remote chip says when this login rides a holder session that outlives the pane. Only a
// login whose own command line names one has it — the wrapper appends `relay-holder <session>`
// to every ssh it lands in a holder — so the argv is the whole truth: a plain `ssh host` typed
// by hand shares the connection but runs no holder, and the chip keeps its ordinary tooltip.
void Pane::updateRemoteHolderChip() {
    // PaneChrome parents itself to the pane (the window that owns the layout builds it), and
    // PaneChrome.h includes Pane.h, so the pointer cannot be a Pane member. A pane has few
    // children; finding the chrome costs nothing beside the argv read that precedes it.
    for (QObject *child : children()) {
        if (auto *chrome = dynamic_cast<PaneChrome *>(child)) {
            chrome->setRemotePersistent(m_login.active ? relay::ssh::holderSession(foregroundArgv())
                                                       : QString());
            return;
        }
    }
}

// Under mosh the ssh that built the connection exits as soon as the session is up, so the
// shared master under it has nothing of Relay's holding it open: ControlPersist expires it ten
// idle minutes after the agent last used the socket, and the host tools go with it. A no-op ssh
// over the master every four minutes resets that clock. An ssh login needs none of this — its
// own session keeps its master — so the timer runs for mosh and mosh-client only, and stops the
// moment the login ends (endLogin, in Pane.h).
void Pane::updateMasterKeepalive() {
    const bool mosh = m_login.program == QStringLiteral("mosh")
        || m_login.program == QStringLiteral("mosh-client");
    if (!(m_login.active && mosh && loginReachable())) {
        if (m_masterKeepalive.isActive()) m_masterKeepalive.stop();
        return;
    }
    if (!m_masterKeepalive.isActive()) {
        m_masterKeepalive.setInterval(4 * 60 * 1000);
        connect(&m_masterKeepalive, &QTimer::timeout, this, &Pane::runMasterKeepalive,
                Qt::UniqueConnection);
        m_masterKeepalive.start();
    }
}

void Pane::runMasterKeepalive() {
    // The login this was armed for may have ended between ticks; endLogin stops the timer, but
    // one tick already in flight still lands here.
    if (!m_login.active || !loginReachable()) {
        m_masterKeepalive.stop();
        return;
    }
    const QStringList argv = relay::ssh::keepaliveArgv(m_login.where.controlPath, loginHost());
    if (argv.isEmpty()) return;
    relay::log::info(QStringLiteral("ssh_master_keepalive pane=%1 host=%2").arg(paneLogId(), loginHost()));
    // Detached: a hung or dead ssh must never take the pane — or Relay — down with it, and the
    // keepalive's lifetime is the master's, not the pane's.
    QProcess::startDetached(argv.first(), argv.mid(1));
}

void Pane::reapLandScripts() const {
    // Card #FYEY, step 6: this pane's conversations are over, so any holds land.py made for its
    // token can be released — uncommitted-work holds building up until sessions die is the whole
    // card. Run only where the checkout really has scripts/land.py (installs do not), detached
    // through a shell that discards output: failure is ignored by design, and until the `reap`
    // subcommand lands (a later step of this card) it exits non-zero and that is fine. Never
    // keyed on idle time: a pane that is merely quiet still holds its work.
    const QString script = QFileInfo(m_data, QStringLiteral("scripts/land.py")).absoluteFilePath();
    if (!QFileInfo(script).isFile()) return;
    const QString command = QStringLiteral("exec python3 '%1' reap --token '%2' >/dev/null 2>&1")
                                .arg(script, m_token);
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}
