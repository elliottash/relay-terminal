// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pane.h"



void Pane::runSlashCommand(const QString &name, const QString &args) {
        if (name == QStringLiteral("new") || name == QStringLiteral("clear")) newChat();
        else if (name == QStringLiteral("model")) {
            // Claude Code and Codex are models here too (26.9): `/model codex`, `/model claude`.
            // The rows offered are the presets — which now include the guests the worker can run
            // as this pane's agent (29.4) — plus the guests only the Tier B launch answers for.
            if (args.isEmpty()) { openModelPicker(); return; }
            {
                // `/model claude opus`: a model named with the guest, which only its harness can
                // honour. Every installed guest is matched here, Tier A or Tier B, so the name
                // always reaches pickGuest rather than a preset whose label happens to contain it.
                const QString firstWord = args.section(QLatin1Char(' '), 0, 0);
                const QString restOfArgs = args.section(QLatin1Char(' '), 1).trimmed();
                for (const QString &id : installedGuests()) {
                    const bool byName = guestDisplayName(id).compare(args, Qt::CaseInsensitive) == 0;
                    if (byName || firstWord.compare(id, Qt::CaseInsensitive) == 0) {
                        pickGuest(id, byName ? QString() : restOfArgs);
                        return;
                    }
                }
                // A catalog entry (owner, 2026-09-20): its key, its model id, or words of its
                // label and provider — the same match the picker's filter makes, shown rows
                // first. relay::modelrows::resolve is that match, so /model in a helper agent's
                // composer answers the same word with the same model (#PK5Q).
                const relay::models::Catalog catalog = modelCatalog();
                // The model's own name first (card #MDL1, rules 1 and 2): `/model gpt-6-sol`
                // names one model however many providers serve it, and `/model
                // gpt-6-sol@openrouter` names which of them takes it. `findByName` folds the
                // shown rows into one group per name and answers the part after the "@"; the
                // older matches below still take a preset id, a key or words of a label.
                // The rows must outlive the pointer into them: `findByName` returns an entry of
                // the list it was handed, and a temporary in an `if` condition is destroyed before
                // the body runs — which segfaulted in `splitKey` on the freed key (Xvfb run,
                // docs/qa_evidence/2026-09-21-model-names-everywhere).
                const QList<relay::models::Entry> rows = relay::models::shown(catalog);
                // Available first (step 2 of four), then every usable entry: a model you
                // un-ticked is out of the lists and the box, not forbidden — typing its name is
                // asking for that model (card #MDL1, design 5.7). Both lists are named locals for
                // the lifetime reason above.
                const relay::models::Entry *named = relay::models::findByName(catalog, rows, args);
                const QList<relay::models::Entry> every =
                    named != nullptr ? QList<relay::models::Entry>() : relay::models::allUsable(catalog);
                if (named == nullptr) named = relay::models::findByName(catalog, every, args);
                if (named != nullptr) {
                    selectEntry(named->key);
                    return;
                }
                if (const QString key = relay::modelrows::resolve(catalog, args); !key.isEmpty()) { selectEntry(key); return; }
                for (const auto &model : std::as_const(m_stored)) {
                    if (model.first.compare(args, Qt::CaseInsensitive) == 0 || model.second.contains(args, Qt::CaseInsensitive)) {
                        leaveGuest([this, id = model.first] { selectModel(id); });
                        return;
                    }
                }
                status(QStringLiteral("No model matches “%1”. /model opens the picker.").arg(args));
                return;
            }
        } else if (name == QStringLiteral("swap")) {
            swapModel();
            hint(QStringLiteral("model.swap.key"), relay::ShortcutHints::nextTime(
                     Keymap::instance().shortcutText(QStringLiteral("agent.swap")), QStringLiteral("swapping models")));
        } else if (name == QStringLiteral("main") || name == QStringLiteral("high")
                   || name == QStringLiteral("flash") || name == QStringLiteral("local")) {
            // The pane's own agent, not the tier table: /flash runs this conversation on the Flash
            // model and /main puts it back, both keeping the conversation (the same switch as Alt+F).
            // /local is the same switch onto a model served on this machine (owner, 2026-09-18),
            // and /high the same switch onto the High tier (owner, 2026-09-21: "add /high") — the
            // hardest turns, which with no high list is the pane's own model at its top level.
            // Saying so even when the pane is already there means the command always reports where
            // it ended up, rather than looking like it did nothing.
            if (m_agentRole == name) {
                const QString model = name == QStringLiteral("main") ? m_model : roleModel(name);
                const QString from = name == QStringLiteral("main") ? m_currentPreset : rolePreset(name);
                status(QStringLiteral("Already on %1%2.").arg(roleLabel(name),
                           model.isEmpty() ? QString() : QStringLiteral(" · ") + modelNameFor(from, model)));
                return;
            }
            // Nothing served here: say so and stay put, rather than switching to a role that would
            // resolve straight back to the main model.
            if (name == QStringLiteral("local") && !hasLocalEndpoint()) { status(noLocalModelMessage()); return; }
            setAgentRole(name);
        } else if (name == QStringLiteral("glm") || name == QStringLiteral("kimi")) {
            // One word for the two providers the owner actually pays for. The Coding Plan preset is
            // tried first so the subscription is spent before pay-as-you-go credit, and the other
            // preset in the family is the fallback; with neither key stored the command says which
            // provider is missing instead of opening a picker.
            const bool glm = name == QStringLiteral("glm");
            const QStringList order = glm ? QStringList{QStringLiteral("glm-coding"), QStringLiteral("glm")}
                                          : QStringList{QStringLiteral("kimi-code"), QStringLiteral("kimi")};
            // No busy check: selectModel accepts a switch mid-turn (issue 3ES1).
            for (const QString &id : order) {
                const auto stored = std::find_if(m_stored.cbegin(), m_stored.cend(),
                                                 [&](const auto &entry) { return entry.first == id; });
                if (stored == m_stored.cend()) continue;
                // The model's own name and the provider serving it (card #MDL1, rule 1):
                // "glm-5.3 · z.ai (glm)", not the preset label "z.ai · glm-5.3 · coding plan",
                // which was being printed where a model belongs. These two commands are about
                // which key is spent, so the provider stays.
                const QString named = presetModelDisplay(id);
                const QString line = named.isEmpty() ? stored->second : named;
                if (id == m_currentPreset && m_agentRole == QStringLiteral("main")) {
                    status(QStringLiteral("Already on %1.").arg(line));
                    return;
                }
                // Through the same one-shot as /swap: this command's own sentence, not the
                // generic one `model_changed` prints a moment later (card #MDL1).
                sayAndSwitch(QStringLiteral("model: %1.").arg(line),
                             [this, id] { selectModel(id); });   // also puts the pane back on main
                return;
            }
            status(QStringLiteral("No stored %1 key. Add one in Options › Models › API keys….")
                       .arg(glm ? QStringLiteral("GLM") : QStringLiteral("Kimi")));
        } else if (name == QStringLiteral("models")) {
            // Owner (card #Y2JW): "/models … should bring to the models options". Since card #MDL1
            // t:a11 that is the **models pane** and not a page of Options: providers, which models
            // are available and their order are its three tabs, so this is one door for all three.
            // `/models` opens it on providers — the word is about the setup — and `/model` alone
            // opens the same pane on priorities, where a pick is made.
            openModelPicker(relay::ModelsPane::providersTab());
        } else if (name == QStringLiteral("profile")) {
            // Owner (2026-09-20 evening): "an 'AI work' profile and an 'admin work' profile that
            // sets different model priorities". A profile is a named set of the five tier lists
            // (Options › Models › profile); switching swaps every list at once, for every pane.
            // The conversation and this pane's model stay: the status line says what main now
            // runs on and how to put this pane on it. That is /swap, not /main — /main only comes
            // off an agent role, and said so wrongly here until card #MDL1.
            namespace curation = relay::models::curation;
            const QStringList names = curation::profiles();
            if (names.isEmpty()) {
                status(QStringLiteral("No profiles yet. Options › Models › profile › new profile… saves the five lists under a name."));
                return;
            }
            const QString current = curation::currentProfile();
            QString chosen = args.trimmed();
            if (chosen.isEmpty()) {
                QList<relay::agentui::PickerRow> rows;
                for (const QString &each : names)
                    rows << relay::agentui::PickerRow{{each, each == current ? QStringLiteral("current") : QString()},
                                                      QStringLiteral("The five tier lists saved as “%1”").arg(each), each};
                const auto result = relay::agentui::pick(this, QStringLiteral("profile"),
                    QStringLiteral("A named set of the five tier lists. Switching swaps every list at once, in every pane."),
                    {QStringLiteral("profile"), QString()}, rows, {{QStringLiteral("use"), QStringLiteral("Use"), true}});
                if (result.row < 0 || result.row >= names.size()) return;
                chosen = names.at(result.row);
            } else if (!names.contains(chosen)) {
                // Typed: the exact name, else the one name it is a case-insensitive prefix of.
                QStringList matches;
                for (const QString &each : names)
                    if (each.compare(chosen, Qt::CaseInsensitive) == 0) matches = QStringList{each};
                if (matches.isEmpty())
                    for (const QString &each : names)
                        if (each.startsWith(chosen, Qt::CaseInsensitive)) matches << each;
                if (matches.size() != 1) {
                    status(QStringLiteral("No profile called “%1”. Profiles: %2.").arg(chosen, names.join(QStringLiteral(", "))));
                    return;
                }
                chosen = matches.first();
            }
            curation::applyProfile(chosen);
            if (onProfileApplied) onProfileApplied();
            else { modelsCurationChanged(); agentOptionsChanged(QStringLiteral("models/fallback")); }
            const relay::models::Entry main = relay::models::mainDefault(modelCatalog());
            status(main.key.isEmpty()
                       ? QStringLiteral("Profile: %1.").arg(chosen)
                       : QStringLiteral("Profile: %1 · main runs on %2 (/swap puts this pane on it).").arg(chosen, main.name));
        } else if (name == QStringLiteral("effort") || name == QStringLiteral("reasoning")) {
            // The levels are the model's own (card #MDL1, 2026-09-21): `/effort xhigh` works on a
            // codex pane and `/effort ultra` with it, while a model whose level is not the pane's
            // to set answers with the reason and changes nothing.
            const QString why = effortFixedReason();
            const QStringList levels = offeredEfforts();
            const QString wanted = args.trimmed().toLower();
            if (!why.isEmpty()) status(sentenceCase(why));
            else if (levels.isEmpty()) status(QStringLiteral("This model has no reasoning level."));
            else if (levels.contains(wanted)) setEffort(wanted);
            else if (wanted.isEmpty())
                effortStep(1 - (levels.indexOf(nearestEffort(levels, m_effort)) == levels.size() - 1 ? levels.size() : 0));
            else if (relay::models::effortRank(wanted) >= 0) setEffort(wanted);   // snaps, and says so
            else status(QStringLiteral("%1 takes %2.")
                            .arg(modelNameFor(m_currentPreset, paneModel()), levels.join(QStringLiteral(", "))));
        } else if (name == QStringLiteral("compact")) compactNow(args);
        else if (name == QStringLiteral("usage-reset")) {
            // The worker's guest spends it or names the page it is spent on (#KQNP); nothing is
            // spent before the person says yes to the question the answer brings.
            if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
            send({{"type", "usage_reset"}});
        }
        else if (name == QStringLiteral("context")) {
            if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
            m_contextNotePending = true;
            send({{"type", "context"}});
            send({{"type", "context_breakdown"}});
        } else if (name == QStringLiteral("terminal")) {
            if (!hasShell()) status(QStringLiteral("This pane has no terminal."));
            else terminalContextMenu();
        } else if (name == QStringLiteral("rewind")) openRewind();
        else if (name == QStringLiteral("rewind-code")) openRewind(QStringLiteral("code"));
        else if (name == QStringLiteral("fork")) requestFork();
        else if (name == QStringLiteral("resume") || name == QStringLiteral("sessions")) {
            openResume(args);
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("sessions.open")); !keys.isEmpty())
                hint(QStringLiteral("resume.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("sessions & projects")));
        } else if (name == QStringLiteral("conversations")) {
            openConversations(args);
            // One pane now: the key that opens it is sessions.open's (#SPSG).
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("sessions.open")); !keys.isEmpty())
                hint(QStringLiteral("conversations.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("sessions & projects")));
        } else if (name == QStringLiteral("status") || name == QStringLiteral("info")
                   || name == QStringLiteral("usage")) {
            openInfo();
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.info")); !keys.isEmpty())
                hint(QStringLiteral("info.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("conversation info")));
        } else if (name == QStringLiteral("find")) {
            openFindInView();
            if (!args.isEmpty() && m_findBar) m_findBar->start(args);
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("find.inView")); !keys.isEmpty())
                hint(QStringLiteral("find.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("find in this pane")));
        }
        else if (name == QStringLiteral("plan")) togglePlanMode();
        else if (name == QStringLiteral("light") || name == QStringLiteral("dark")) {
            // The owner named the two (0EXJ, 2026-09-18: "light activates beige; dark activates
            // copper"), so these are not "any light theme" — they are those two theme files. The
            // switch is the one the settings picker makes, which restyles the chrome, both
            // terminal engines and the prompt box's colours and stores `theme/name`.
            chooseTheme(name == QStringLiteral("light") ? QStringLiteral("ibm-beige") : QStringLiteral("dark-copper"));
        }
        else if (name == QStringLiteral("theme")) {
            // Every theme, not just the owner's two (owner, 2026-09-19): `/theme gruvbox`, or the
            // bare command for a list. Matched on the id, then the name, then a part of either.
            const QList<relay::theme::ThemeChoice> themes = relay::theme::availableThemes();
            if (!args.isEmpty()) {
                // `random` is answered before any name match, because a theme file may be called
                // anything and this word is the command's own (Options › Appearance › Randomize,
                // card #R4ND).
                if (args.compare(QStringLiteral("random"), Qt::CaseInsensitive) == 0
                    || args.compare(QStringLiteral("randomize"), Qt::CaseInsensitive) == 0
                    || args.compare(QStringLiteral("shuffle"), Qt::CaseInsensitive) == 0) {
                    randomTheme();
                    return;
                }
                const auto find = [&](auto test) { for (const auto &t : themes) if (test(t)) return t.id; return QString(); };
                QString id = find([&](const auto &t) { return t.id.compare(args, Qt::CaseInsensitive) == 0 || t.name.compare(args, Qt::CaseInsensitive) == 0; });
                if (id.isEmpty()) id = find([&](const auto &t) { return t.id.contains(args, Qt::CaseInsensitive) || t.name.contains(args, Qt::CaseInsensitive); });
                if (id.isEmpty()) { status(QStringLiteral("No theme matches “%1”.").arg(args)); return; }
                chooseTheme(id);
                return;
            }
            QList<relay::agentui::PickerRow> rows;
            for (const auto &t : themes)
                rows << relay::agentui::PickerRow{{t.name, t.id == relay::theme::activeThemeId() ? QStringLiteral("current") : QString()}, t.description, t.id};
            // Last row, after the themes it picks from (card #R4ND).
            rows << relay::agentui::PickerRow{{QStringLiteral("Random"), QString()},
                                              QStringLiteral("A theme at random, never the one you are on"),
                                              QStringLiteral("random")};
            const auto result = relay::agentui::pick(this, QStringLiteral("Theme"), QStringLiteral("The theme for this tab, and for new tabs unless Options › Appearance says a command changes one tab only."),
                                                     {QStringLiteral("Theme"), QString()}, rows, {{QStringLiteral("use"), QStringLiteral("Use"), true}});
            if (result.row == themes.size()) randomTheme();
            else if (result.row >= 0 && result.row < themes.size()) chooseTheme(themes.at(result.row).id);
        }
        else if (name == QStringLiteral("rename")) {
            // With a name it renames straight away; without one it opens the same editor a double
            // click does, where clearing the field hands the pane back to the model.
            if (args.isEmpty()) beginRename(); else renameTo(args);
        }
        else if (name == QStringLiteral("rename-tab")) {
            if (onRenameTab) onRenameTab(args, args.isEmpty());
        }
        else if (name == QStringLiteral("join") || name == QStringLiteral("connect")) {
            // The code is public and four letters; the PIN is asked for in the dialog, never on
            // the command line, where it would land in the prompt history.
            if (onJoinShared) onJoinShared(args.trimmed().toUpper());
        }
        else if (name == QStringLiteral("update")) {
            // The window runs scripts/relay-update.py and shows each of its lines as the notice;
            // on its UPDATED marker it restarts Relay into the version it just installed.
            if (onUpdateApp) onUpdateApp();
        }
        else if (name == QStringLiteral("restart")) {
            if (onRestartApp) onRestartApp();
        }
        else if (name == QStringLiteral("board") || name == QStringLiteral("switchboard")) {
            if (onOpenBoard) onOpenBoard();
            boardShortcutHint(QStringLiteral("board.slash"));
        }
        else if (name == QStringLiteral("card")) {
            if (args.trimmed().isEmpty()) { status(QStringLiteral("Usage: /card <what to remember>")); return; }
            fileCard(args);
        }
        else if (name == QStringLiteral("init")) {
            // Trigger (5): the explicit command. It asks even about a project the user declined
            // once — typing it is changing your mind — and in a directory that is no project at
            // all it offers to treat the pane's own directory as one.
            QString why;
            if (!askProjectInit(relay::projectinit::Trigger::InitCommand, QString(), &why))
                status(why.isEmpty() ? QStringLiteral("Nothing to initialize here.") : why);
        }
        else if (name == QStringLiteral("recap")) requestRecap();
        else if (name == QStringLiteral("tasks") || name == QStringLiteral("requests") || name == QStringLiteral("todos")) {
            openRequests();
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests")); !keys.isEmpty() && requestsOpen())
                hint(QStringLiteral("tasks.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("task list")));
        }
        else if (name == QStringLiteral("continue")) continueTurn(true);   // the slow path: it teaches the empty-box key (#SXF1)
        else if (name == QStringLiteral("instructions")) openInstructions();
        else if (name == QStringLiteral("export")) exportConversation();
        else if (name == QStringLiteral("speak")) {
            // `/speak stop` silences whichever pane is reading: the voice is one, app-wide.
            auto &speaker = relay::speech::Speaker::instance();
            if (args.trimmed().compare(QStringLiteral("stop"), Qt::CaseInsensitive) != 0) readLastReplyAloud();
            else if (speaker.speaking()) speaker.stop();
            else status(QStringLiteral("Nothing is being read aloud."));
        }
        else if (name == QStringLiteral("agents")) {
            if (onShowAgents) onShowAgents();
            else { m_agentsListPending = true; send({{"type", "agents_list"}, {"workspace", m_workspace}}); }
        } else if (name == QStringLiteral("skill")) {
            if (args.isEmpty()) { openSkills(QString(), true); return; }
            const QString text = QStringLiteral("/skill ") + args;
            if (skillFor(text).isEmpty()) {
                const QString wanted = args.section(QRegularExpression(QStringLiteral("\\s")), 0, 0);
                QStringList known;
                for (const auto &skill : std::as_const(m_skillCommands)) known << skill.name;
                const QStringList close = relay::slash::closest(wanted, known, 2);
                status(close.isEmpty() ? QStringLiteral("No skill named “%1” · /skills lists them").arg(wanted)
                                       : QStringLiteral("No skill named “%1” · did you mean %2?").arg(wanted, close.join(QStringLiteral(" or "))));
                return;
            }
            submitAgent(text, false);
        } else if (name == QStringLiteral("skills")) {
            openSkills(args.trimmed(), true);
        } else if (name == QStringLiteral("help")) {
            // The same popup `?` shows, never a second surface for the same list. Typing the
            // command while the popup is up must leave it up, so this shows rather than toggles.
            if (!(m_helpPopup && m_helpPopup->isVisible())) toggleHelpPopup();
            hint(QStringLiteral("help.slash"), QStringLiteral("Next time: press ? in an empty prompt box"));
        }
    }


