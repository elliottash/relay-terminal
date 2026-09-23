// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"
#include "WindowManagerImpl.h"

QList<relay::SettingsSection> RelayWindow::settingsSections() {
        QList<relay::SettingsSection> sections;
        QSettings settings;

        relay::SettingsSection general;
        general.id = QStringLiteral("general");
        general.title = QStringLiteral("General");
        general.blurb = QStringLiteral("What Relay shows while it works.");
        // Reasoning display (issue T8CN): three ways, not a bool — the reasoning streams into a
        // fold under a ✦ line that collapses when the block ends (collapse), stays open (always),
        // or never draws and leaves the single ✦ summary line (never). The row reads through
        // Pane::thinkingDisplay() so a not-yet-migrated agent/show_thinking shows as what it maps
        // to, and choosing writes the new key (the old one is retired by the same read).
        {
            relay::SettingRow thinking = choiceRow(QStringLiteral("option:thinking_display"),
                                                  QStringLiteral("Thinking display"),
                                                  QStringLiteral("How the agent's reasoning is shown: folded into the "
                                                                 "terminal and collapsed when it ends, always open, "
                                                                 "or only the ✦ summary line"),
                                                  {QStringLiteral("collapse"), QStringLiteral("always"), QStringLiteral("never")},
                                                  {QStringLiteral("Collapse when done"), QStringLiteral("Always open"), QStringLiteral("Never show")},
                                                  Pane::thinkingDisplay(), QStringLiteral("collapse"),
                                                  [this](const QString &value) {
                                                      QSettings().setValue(QStringLiteral("agent/thinking_display"), value);
                                                      if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/thinking_display"));
                                                  });
            thinking.aliases = QStringLiteral("reasoning thinking traces show_thinking");
            general.rows << thinking;
        }
        general.rows << toggleRow(QStringLiteral("agent/show_tool_output"), QStringLiteral("Show tool output"),
                                  QStringLiteral("Print what each tool returned, not only the one-line summary"), false);
        {
            relay::SettingRow desktop;
            desktop.kind = relay::SettingRow::Toggle;
            desktop.id = QStringLiteral("option:notifications/desktop");
            desktop.label = QStringLiteral("Desktop notifications");
            desktop.detail = QStringLiteral("When the agent finishes or needs you and this window is not in front");
            desktop.aliases = QStringLiteral("notify alerts popup bell toast");
            desktop.checked = relay::NotificationCenter::desktopEnabled();
            desktop.changed = !desktop.checked;
            desktop.onToggle = [](bool on) { relay::NotificationCenter::setDesktopEnabled(on); };
            desktop.reset = [] { relay::NotificationCenter::setDesktopEnabled(true); };   // on when Relay ships
            general.rows << desktop;
        }
        {
            relay::SettingRow hints;
            hints.kind = relay::SettingRow::Toggle;
            hints.id = QStringLiteral("option:shortcut_hints");
            hints.label = QStringLiteral("Shortcut hints");
            hints.detail = QStringLiteral("A brief tip when you do something the slow way and a key exists");
            hints.checked = relay::ShortcutHints::instance().enabled();
            hints.changed = !hints.checked;
            hints.onToggle = [](bool on) { relay::ShortcutHints::instance().setEnabled(on); };
            // On when Relay ships. How often each hint has been shown is not a setting and is not
            // touched here; Actions › Reset shortcut hints is what forgets those counts.
            hints.reset = [] { relay::ShortcutHints::instance().setEnabled(true); };
            general.rows << hints;
        }
        general.rows << toggleRow(QStringLiteral("recap/away"), QStringLiteral("Recap when you come back"),
                                  QStringLiteral("Written into a pane a few minutes after work ends there unwatched"), true);
        {
            // Dolphin-style opening in the explorer pane and the preview file picker (issue #0C7V).
            relay::SettingRow single = toggleRow(QStringLiteral("files/single_click"),
                                                 QStringLiteral("Open items with a single click"),
                                                 QStringLiteral("In the file explorer and the file picker; Ctrl+click and Shift+click still select"),
                                                 true, [this](bool) { applySingleClickSetting(); });
            single.aliases = QStringLiteral("dolphin explorer double click files folders");
            general.rows << single;
        }
        {
            // Saved window layout. The toggle only decides whether the layout is kept; restoring the
            // last closed pane works either way.
            relay::SettingRow reopen;
            reopen.kind = relay::SettingRow::Toggle;
            reopen.id = QStringLiteral("option:windows/restore");
            reopen.label = QStringLiteral("Reopen windows on start");
            reopen.detail = QStringLiteral("Windows, tabs, panes, directories and conversations come back");
            reopen.aliases = QStringLiteral("session persist startup warp layout remember where you left off");
            reopen.checked = WindowManager::restoreEnabled();
            reopen.changed = !reopen.checked;    // on when Relay ships
            reopen.onToggle = [this](bool on) {
                QSettings().setValue(QStringLiteral("windows/restore"), on);
                if (on) m_manager->scheduleSave(); else m_manager->forgetSavedLayout(false);
                notice(on ? QStringLiteral("Relay will reopen this window set on start.")
                                           : QStringLiteral("Relay will open one new window on start."), 6000);
            };
            // The same two effects as the toggle, without its notice: the reset has one of its own.
            reopen.reset = [this] {
                QSettings().remove(QStringLiteral("windows/restore"));
                if (WindowManager::restoreEnabled()) m_manager->scheduleSave();
                else m_manager->forgetSavedLayout(false);
            };
            general.rows << reopen;
        }
        // Which releases /update offers (card #HDA9). It is an option and not a flag the user types,
        // because /update is the whole surface: the channel has to be answered before the command
        // runs. The updater's own default is the same "all", so an unset key and the shipped row
        // agree.
        general.rows << headingRow(QStringLiteral("Updates"));
        {
            relay::SettingRow channel = choiceRow(QStringLiteral("option:update/channel"),
                                                  QStringLiteral("Update channel"),
                                                  QStringLiteral("Which releases /update offers: every published "
                                                                 "release, betas included, or only the finished ones"),
                                                  {QStringLiteral("all"), QStringLiteral("stable")},
                                                  {QStringLiteral("All releases, betas included"),
                                                   QStringLiteral("Stable releases only")},
                                                  updateChannel(), QStringLiteral("all"),
                                                  [](const QString &value) {
                                                      if (value == QStringLiteral("all"))
                                                          QSettings().remove(QStringLiteral("update/channel"));
                                                      else
                                                          QSettings().setValue(QStringLiteral("update/channel"), value);
                                                  });
            channel.aliases = QStringLiteral("update upgrade release beta prerelease stable channel version");
            general.rows << channel;
        }

        // Diagnostics (issue SQAM): how much is logged persists, so it is an option; opening the log
        // folder is something you do, so it is in Actions (Relay › Open the log folder).
        general.rows << headingRow(QStringLiteral("Diagnostics"));
        {
            QStringList ids, labels;
            QString about;
            const QString current = relay::log::levelName(relay::log::level());
            for (const QStringList &choice : relay::log::levelChoices()) {
                ids << choice.at(0); labels << choice.at(1);
                if (choice.at(0) == current) about = choice.at(2);
            }
            relay::SettingRow level = choiceRow(QStringLiteral("option:log_level"), QStringLiteral("Log detail"),
                                                about + (current == QStringLiteral("verbose")
                                                             ? QStringLiteral(" · prompts are written to the log file")
                                                             : QStringLiteral(" · agent workers pick it up when they restart")),
                                                ids, labels, current, QStringLiteral("info"),
                                                [](const QString &id) { relay::log::setLevel(id); });
            level.aliases = QStringLiteral("log logs diagnostics debug verbose troubleshoot");
            general.rows << level;
        }
        sections << general;

        // Colour themes (issue 0JA7). One theme file carries the UI tokens and the 16-colour
        // terminal palette, so the picker restyles the chrome, both terminal engines and the
        // prompt box's syntax colours at once. The Settings pane's search reaches these rows too.
        relay::SettingsSection appearance;
        appearance.id = QStringLiteral("appearance");
        appearance.title = QStringLiteral("Appearance");
        {
            auto fontSize = numberRow(QStringLiteral("appearance/font_size"), QStringLiteral("Font size"),
                                      QStringLiteral("Default terminal text size in all panes; Ctrl+0 resets zoom to this size"),
                                      11, 6, 48, QStringLiteral(" pt"));
            const auto apply = [] {
                for (QWidget *widget : QApplication::allWidgets())
                    if (auto *pane = dynamic_cast<Pane *>(widget)) pane->applyTerminalSettings();
            };
            const auto change = fontSize.onNumber;
            fontSize.onNumber = [this, change, apply](int value) {
                change(value);
                apply();
                hint(QStringLiteral("terminal.fontSize.options"), relay::ShortcutHints::nextTime(
                    Keymap::instance().shortcutText(QStringLiteral("terminal.zoomIn")), QStringLiteral("zoom this pane")));
            };
            const auto reset = fontSize.reset;
            fontSize.reset = [reset, apply] { reset(); apply(); };
            appearance.rows << fontSize;
        }
        appearance.blurb = QStringLiteral("One file per theme. Built-in themes ship with Relay; your own go in "
                                          "~/.config/relay/themes as <name>.toml — copy a built-in one and edit it.");
        {
            QStringList ids, labels;
            for (const relay::theme::ThemeChoice &choice : relay::theme::availableThemes()) {
                ids << choice.id;
                labels << (choice.builtin ? choice.name : choice.name + QStringLiteral(" (yours)"));
            }
            appearance.rows << choiceRow(QStringLiteral("option:theme"), QStringLiteral("Theme"),
                                         QStringLiteral("What Relay opens on and what a new tab starts with; this tab takes "
                                                        "it at once, and so do /light, /dark and /theme unless turned off below"),
                                         ids, labels, relay::theme::startupThemeId(),
                                         relay::theme::defaultThemeId(), [this](const QString &id) {
                if (!chooseTheme(id, m_tabs->currentWidget(), true)) {
                    notice(QStringLiteral("That theme could not be read."), 6000);
                    return;
                }
                notice(QStringLiteral("Theme: %1.").arg(relay::theme::active().name), 4000);
            });
        }
        // "Randomize" (card #R4ND, owner 2026-09-20): a theme you did not pick, and a different one
        // every press. It is a button and not a stored mode — what it lands on *is* the theme, as
        // if it had been chosen from the list above — so there is nothing here to reset.
        {
            relay::SettingRow shuffle = buttonRow(QStringLiteral("option:theme_random"), QStringLiteral("Randomize"),
                                                  QStringLiteral("Take a theme at random: never the one you are on, and a "
                                                                 "different one each press"),
                                                  QStringLiteral("Randomize"), [this] { randomizeTheme(); });
            shuffle.aliases = QStringLiteral("random randomise shuffle surprise dice any theme");
            // Undoable in one click — the list above is right there — so an agent asked to
            // randomize the theme may press it (owner decision 2, card #FEJQ).
            shuffle.agentSafeButtons = {0};
            appearance.rows << shuffle;
        }
        // Owner, 2026-09-19: "add an option, on by default, that themes are tab specific. and add
        // an option, off by default, to start tabs with a new theme."
        appearance.rows << toggleRow(QStringLiteral("theme/per_tab"), QStringLiteral("Each tab keeps its own theme"),
                                     QStringLiteral("/light, /dark and /theme change the tab you are in; switching tabs switches "
                                                    "the theme"),
                                     true, [this](bool on) {
            if (on) applyTabTheme(m_tabs->currentWidget());
            else relay::theme::setActiveTheme(relay::theme::startupThemeId(), false);
            m_tabs->tabBar()->update();
        });
        appearance.rows << toggleRow(QStringLiteral("theme/commands_set_default"),
                                     QStringLiteral("/light, /dark and /theme also set the default"),
                                     QStringLiteral("The theme a command picks becomes what new tabs start with and what Relay opens "
                                                    "on; off, a command changes only the tab you are in"),
                                     true);
        appearance.rows << toggleRow(QStringLiteral("theme/new_tab_new_theme"), QStringLiteral("Start each new tab on the next theme"),
                                     QStringLiteral("A new tab takes the next theme in the list instead of the default, so "
                                                    "tabs are easy to tell apart"),
                                     false, [this](bool on) {
            // One way of telling tabs apart at a time.
            if (on) { QSettings().setValue(QStringLiteral("theme/randomize_new_tab"), false); refreshSettingsPanes(); }
        });
        // Card #R4ND, owner 2026-09-20: "i meant a persistent mode. it randomizes on each new
        // tab." The button above is the one-shot; this is the mode.
        appearance.rows << toggleRow(QStringLiteral("theme/randomize_new_tab"), QStringLiteral("Start each new tab on a random theme"),
                                     QStringLiteral("A new tab takes a theme drawn at random — never the default and never the "
                                                    "previous tab's — so tabs are easy to tell apart"),
                                     false, [this](bool on) {
            if (on) { QSettings().setValue(QStringLiteral("theme/new_tab_new_theme"), false); refreshSettingsPanes(); }
        });
        {
            // Pane header colours (#SPBN): the pane chrome reads the key; refreshAll() repaints.
            relay::SettingRow colours = choiceRow(QStringLiteral("option:pane_colours"), QStringLiteral("Pane colours"),
                                                  QStringLiteral("Tint a pane's header by what it is: each type its own, one per "
                                                                 "group (tools, agents), or none. A remote shell is always marked"),
                                                  relay::panestatus::colourModeIds(), relay::panestatus::colourModeLabels(),
                                                  relay::panestatus::colourModeId(relay::panestatus::colourModeFrom(
                                                      QSettings().value(QStringLiteral("appearance/pane_colours")).toString())),
                                                  relay::panestatus::colourModeId(relay::panestatus::ColourMode::ByType),
                                                  [](const QString &id) {
                QSettings().setValue(QStringLiteral("appearance/pane_colours"), id);
                PaneChrome::refreshAll();
                // The title-bar buttons wear the same tints while their pane is open.
                refreshChromeButtonsEverywhere();
            });
            colours.aliases = QStringLiteral("color colors colour header tint band pane type group");
            appearance.rows << colours;
        }
        {
            // Pane CPU / memory meters (issue #D03W): the chip beside each pane's title and the
            // suffix in the tab label. The poll re-reads the key within its next tick.
            relay::SettingRow usage = toggleRow(QStringLiteral("appearance/pane_usage"),
                                               QStringLiteral("Pane CPU and memory"),
                                               QStringLiteral("A small CPU % and memory % beside each pane's title, "
                                                              "in the tab label and tooltip, and on the conversation's row "
                                                              "in Sessions, while the pane is using the machine"),
                                               true, [this](bool) { refreshPaneStatus(); });
            usage.aliases = QStringLiteral("cpu memory ram usage load percent meter resource");
            appearance.rows << usage;
        }
        appearance.rows << toggleRow(QStringLiteral("appearance/auto_dim"), QStringLiteral("Dim while working"),
            QStringLiteral("Dim working agents; reveal questions, blocked work and completion. Manual dimming survives completion."), false);
        {
            auto activeDim = toggleRow(QStringLiteral("appearance/auto_dim_active"), QStringLiteral("Include active pane"),
                QStringLiteral("Also dim the selected pane while its agent works. Applies when Dim while working is on."), false);
            activeDim.indent = 1;
            appearance.rows << activeDim;
        }
        appearance.rows << toggleRow(QStringLiteral("appearance/focus_mode"), QStringLiteral("Focus mode"),
            QStringLiteral("Dim other panes while you work in the selected pane. Attention reveals a pane without moving focus."), false);
        appearance.rows << numberRow(QStringLiteral("appearance/dim_strength"), QStringLiteral("Dimming strength"),
            QStringLiteral("Default dimming amount. Alt+wheel adjusts the hovered pane; Alt++/− adjusts the active pane. Up/+ brightens."),
            90, 0, 95, QStringLiteral("%"));
        sections << appearance;

        sections << modelsSection();

        // Right after Models, because a local endpoint is one more thing the model dropdown can
        // offer — it just has no key, so it is not in the API keys dialog (card #24XJ).
        sections << localModels().section();

        relay::SettingsSection terminal;
        terminal.id = QStringLiteral("terminal");
        terminal.title = QStringLiteral("Terminal");
        terminal.blurb = QStringLiteral("The shell side of a pane.");
        terminal.rows << toggleRow(QStringLiteral("composer/history_suggestions"),
                                   QStringLiteral("Command suggestions from history"),
                                   QStringLiteral("→ or Ctrl+F accepts, Alt+→ accepts a word"), true);
        {
            const QString current = Pane::defaultControl();
            terminal.rows << choiceRow(QStringLiteral("option:control_default"),
                                       QStringLiteral("Control when a full-screen program starts"),
                                       QStringLiteral("Who gets the keyboard when vim, less or top opens"),
                                       {QStringLiteral("agent"), QStringLiteral("human")},
                                       {QStringLiteral("The prompt box keeps the keyboard"),
                                        QStringLiteral("Relay takes control for you")},
                                       current, QStringLiteral("agent"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("control/default"), value);
            });
        }
        // The key stays `terminal/copy_on_select` although the behaviour is no longer terminal-only
        // (src/CopyOnSelect.h): renaming it would turn the setting off for everyone who had it on.
        terminal.rows << toggleRow(QStringLiteral("terminal/copy_on_select"), QStringLiteral("Copy on select"),
                                   QStringLiteral("Highlighting text copies it, in the terminal and in read-only panes"), false);
        // Owner, 2026-09-19: "clickable things need to be understood from colors" — and "make the
        // 'add color in program output' an option that is on by default".
        terminal.rows << toggleRow(QStringLiteral("terminal/colour_links"),
                                   QStringLiteral("Colour paths and links in output"),
                                   QStringLiteral("A file, folder, URL or #card Relay can open is green at rest, not only under "
                                                  "the pointer; a colour a program chose stays"),
                                   true, [this](bool) { for (Pane *pane : allPanes()) pane->applyTerminalSettings(); });
        {
            const QString current = QSettings().value(QStringLiteral("terminal/echo_band"), QStringLiteral("channel")).toString();
            terminal.rows << choiceRow(QStringLiteral("option:echo_band"),
                                       QStringLiteral("Band behind what you typed"),
                                       QStringLiteral("The line you sent sits on a band: the channel's colour (cyan shell, violet agent), "
                                                      "the theme's chrome, or none"),
                                       {QStringLiteral("channel"), QStringLiteral("chrome"), QStringLiteral("none")},
                                       {QStringLiteral("Channel colour"), QStringLiteral("Theme chrome"), QStringLiteral("None")},
                                       current, QStringLiteral("channel"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("terminal/echo_band"), value);
            });
        }
        terminal.rows << toggleRow(QStringLiteral("terminal/shell_integration"),
                                   QStringLiteral("Shell integration (OSC 7/133)"),
                                   QStringLiteral("Directory and prompt marks; applies to new panes"), false);
        // SSH sessions (#S5SH, docs/SSH-AND-MOSH.md): the wrapper is set up when a pane's shell
        // starts, so the mode applies to new panes; the host lists are read at each login.
        terminal.rows << headingRow(QStringLiteral("SSH"));
        {
            const QString current = QSettings().value(QStringLiteral("ssh/enhance"), QStringLiteral("auto")).toString();
            relay::SettingRow row = choiceRow(QStringLiteral("option:ssh_enhance"), QStringLiteral("SSH sessions"),
                                              QStringLiteral("What Relay adds to an ssh you type (below); applies to new panes"),
                                              {QStringLiteral("auto"), QStringLiteral("ask"), QStringLiteral("off")},
                                              {QStringLiteral("Enhance automatically"), QStringLiteral("Ask for each host"),
                                               QStringLiteral("Off — plain ssh")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("ssh/enhance"), value);
            });
            row.aliases = QStringLiteral("mosh remote host wrapper controlmaster enhance");
            terminal.rows << row;
        }
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:ssh_enhance");
            info.label = QStringLiteral("Enhancing adds OpenSSH connection sharing to the ssh you type, so the agent can run "
                                        "commands on that host over your login without asking for it again, and loads prompt "
                                        "marks into the remote shell for that login only. Nothing is installed on the host.");
            terminal.rows << info;
        }
        terminal.rows << hostListRow(QStringLiteral("ssh/hosts_never"), QStringLiteral("Never enhance on"),
                                     QStringLiteral("Hosts, comma separated: always plain ssh there"));
        terminal.rows << hostListRow(QStringLiteral("ssh/hosts_always"), QStringLiteral("Always enhance on"),
                                     QStringLiteral("Hosts, comma separated: enhanced without asking in Ask mode"));
        sections << terminal;

        relay::SettingsSection agent;
        agent.id = QStringLiteral("agent");
        agent.title = QStringLiteral("Agent");
        agent.blurb = QStringLiteral("Instructions, skills and the Board. Most of these apply to the "
                                     "next conversation; the timeout rows apply at once.");
        agent.rows << headingRow(QStringLiteral("Instructions and skills"));
        {
            // Where a new pane's prompt box starts; Ctrl+I cycles auto → terminal → agent in the pane.
            const QString current = Pane::defaultInputMode();
            agent.rows << choiceRow(QStringLiteral("option:input_default"),
                                    QStringLiteral("Default input for new sessions"),
                                    QStringLiteral("Ctrl+I cycles auto → terminal → agent; ! and * override one line"),
                                    {QStringLiteral("auto"), QStringLiteral("shell"), QStringLiteral("agent")},
                                    {QStringLiteral("Auto"), QStringLiteral("Terminal"), QStringLiteral("Agent")},
                                    current, QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("input/default"), value);
            });
        }
        agent.rows << buttonRow(QStringLiteral("agent.instructions"), QStringLiteral("Instructions"),
                                QStringLiteral("CLAUDE.md, AGENTS.md, WARP.md and other instruction files"),
                                QStringLiteral("Choose…"), [this] { runAction(QStringLiteral("agent.instructions")); });
        agent.rows << buttonRow(QStringLiteral("option:skills"), QStringLiteral("Skills"),
                                QStringLiteral("List, exclude, refine, import from a repository · /skills"),
                                QStringLiteral("Open…"), [this] { if (m_active) m_active->openSkills(); });
        agent.rows << textRow(QStringLiteral("skills/exclude_text"), QStringLiteral("Excluded skills"),
                              QStringLiteral("Comma-separated names to skip (empty: the default Warp-app list)"),
                              QStringLiteral("name, other-name"), [](const QString &value) {
            QStringList names;
            for (const QString &name : value.split(',', Qt::SkipEmptyParts))
                if (!name.trimmed().isEmpty()) names << name.trimmed();
            QSettings settings;
            settings.setValue(QStringLiteral("skills/exclude_text"), value);
            if (names.isEmpty()) settings.remove(QStringLiteral("skills/exclude"));
            else settings.setValue(QStringLiteral("skills/exclude"), names);
        });
        {
            // The one row whose value is a folder on this machine, so it is the one row with a
            // Browse… button beside the box (card #XZZB); the box still takes a typed path.
            relay::SettingRow plans = textRow(QStringLiteral("agent/plans_dir"), QStringLiteral("Plans folder"),
                                              QStringLiteral("Absolute folder for plans (empty: <project>/.relay/plans)"),
                                              QStringLiteral("<project>/.relay/plans"));
            plans.browse = true;
            agent.rows << plans;
        }
        {
            // Owner decision 3 (card #FEJQ, 2026-09-20): one toggle gates the helper agent and
            // every pane agent together, and it is on when Relay ships — "the main pane agent gets
            // the write tools on by default". It is not an approval prompt (§30.8): the safety net
            // is that every change announces itself and is undoable in one click.
            relay::SettingRow writes = toggleRow(QStringLiteral("agent/app_writes"),
                                                 QStringLiteral("Agents may change options and run actions"),
                                                 QStringLiteral("An agent can set an option on this page, run a "
                                                                "reversible action and open a pane at a row. "
                                                                "Every change says so and can be undone; API keys "
                                                                "are never settable."),
                                                 true, [this](bool) { sendAppCatalog(); });
            writes.aliases = QStringLiteral("agent control app tools options actions writes permission");
            agent.rows << writes;
        }
        {
            // #GMCF decision 7: what a pane sends before the first user word. The full profile is
            // ~14,500 tokens of prompt and tool schemas, which a hosted provider caches and a model
            // served on this machine prefills at about 800 tokens a second — eighteen seconds of
            // silence on every cold turn. "Auto" sends the short profile (18 rules, 8 tools, plus
            // the five Switchboard tools when a project is attached) to a local endpoint, a model
            // on the Lite list of Options › Models (owner, 2026-09-20) or a model with a window of
            // 32k or less, and the full one to everything else; the other two pin it for panes
            // whose owner disagrees.
            relay::SettingRow profile =
                choiceRow(QStringLiteral("agent/prompt_profile"), QStringLiteral("Prompt profile"),
                          QStringLiteral("Auto: the short prompt on a local, Lite-tier or "
                                         "small-window model, the full one elsewhere"),
                          {QStringLiteral("auto"), QStringLiteral("full"), QStringLiteral("short")},
                          {QStringLiteral("Auto"), QStringLiteral("Full"), QStringLiteral("Short")},
                          QSettings().value(QStringLiteral("agent/prompt_profile"),
                                            QStringLiteral("auto")).toString(),
                          QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/prompt_profile"), value);
            });
            profile.aliases = QStringLiteral("prompt profile short full local lite tier context window tokens prefill speed");
            agent.rows << profile;
        }
        agent.rows << headingRow(QStringLiteral("Board"));
        // There is no "where does a new board go" row any more (owner, 2026-09-21, #1CXD): a new
        // board is `board/` whatever any setting says (`relay::projects::newBoardFolder()`), and a
        // board that already exists moves only through the explicit "Move this board to board/"
        // action on the board itself (protocol 19.17). The old `board/hidden_folder` key is left
        // where it is in QSettings: nothing reads it, so nothing has to migrate.
        {
            // Signals (#AQ6X decision 9, owner: "i think yes by default, but its optional"). The
            // flag itself lives in the *board's* `board.yaml` (`signals: {auto_work: …}`), because
            // it is a property of a project and not of this installation — a checkout whose tests
            // are expected to be red does not want threads started on them. So the row writes
            // through the tab's Switchboard worker (`signals_config`, protocol §32.2) and keeps a
            // copy in the settings, which is what it can draw itself from: Options is built before
            // any worker has answered, and a row that showed nothing until one did would read as
            // off. A tab with no project attached has no board to write to and says so.
            relay::SettingRow work =
                toggleRow(QStringLiteral("board/signals_auto_work"),
                          QStringLiteral("Work signals unasked"),
                          QStringLiteral("A failing test nobody is on starts its own agent thread; "
                                         "you get a notification that opens it, and it is listed "
                                         "in Sessions."),
                          true, [this](bool on) {
                QWidget *page = m_tabs ? m_tabs->currentWidget() : nullptr;
                if (!page) return;
                if (relay::projects::boardDirOf(boardWorkspaceOfTab(page)).isEmpty()) {
                    notice(QStringLiteral("This tab has no project attached, so there is no "
                                          "Board to set that on."), 6000);
                    return;
                }
                sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("signals_config")},
                                               {QStringLiteral("auto_work"), on}});
            });
            work.aliases = QStringLiteral("signals signal thread unasked auto work failing test fix "
                                          "agent switchboard board");
            agent.rows << work;
        }
        {
            // The personal inbox board was dropped 2026-09-19 (#916B): a card filed in a tab with
            // no project attached now goes here if it is set, else through the project picker.
            relay::SettingRow defaultProject =
                textRow(QString::fromLatin1(relay::projects::kDefaultProjectSetting),
                       QStringLiteral("Default project for loose cards"),
                       QStringLiteral("Where a card goes when filed with no project attached (empty: ask each time)"),
                       QStringLiteral("(ask each time)"));
            defaultProject.browse = true;
            defaultProject.aliases = QStringLiteral("inbox loose card project picker default switchboard");
            agent.rows << defaultProject;
        }
        {
            relay::SettingRow projects;
            projects.kind = relay::SettingRow::Buttons;
            projects.id = QStringLiteral("projects:manage");
            projects.label = QStringLiteral("Projects");
            projects.detail = QStringLiteral("Manage known projects, active sessions and global knowledge");
            projects.aliases = QStringLiteral("known projects forget remove registry declined switchboard globals");
            projects.buttonTexts = QStringList{QStringLiteral("Open Projects")};
            projects.agentSafeButtons = QList<int>{0};
            projects.onButton = [this](int) {
                openSessions(QStringLiteral("projects"));
                hint(QStringLiteral("projects.options"), relay::ShortcutHints::nextTime(
                    Keymap::instance().shortcutText(QStringLiteral("sessions.open"))));
            };
            agent.rows << projects;
        }
        agent.rows << headingRow(QStringLiteral("Turn limits"));
        agent.rows << textRow(QStringLiteral("agent/compact_threshold"), QStringLiteral("Compaction threshold"),
                              QStringLiteral("Fraction of the model window, 0.50–0.98 (empty: 80% minus output room)"),
                              QStringLiteral("0.80"), [](const QString &value) {
            bool ok = false;
            const double number = value.toDouble(&ok);
            if (value.isEmpty()) QSettings().remove(QStringLiteral("agent/compact_threshold"));
            else if (ok && number >= 0.5 && number <= 0.98)
                QSettings().setValue(QStringLiteral("agent/compact_threshold"), number);
        });
        agent.rows << toggleRow(QStringLiteral("agent/clear_tool_results"), QStringLiteral("Clear old tool results"),
                                QStringLiteral("Keep recent tool results; shorten older large results in one batch"), true);
        agent.rows << toggleRow(QStringLiteral("agent/compact_over_enabled"),
                                QStringLiteral("Compact when a prompt exceeds the size limit"),
                                QStringLiteral("Between turns only; the model-window limit still applies"), false);
        agent.rows << numberRow(QStringLiteral("agent/compact_over_tokens"), QStringLiteral("Prompt size limit"),
                                QStringLiteral("Used when size-based compaction is on; 256,000 tokens by default"),
                                256000, 8000, 10000000, QStringLiteral(" tokens"));
        // Idle deadline for a model call (protocol 15). Applies to the running agent at once.
        {
            relay::SettingRow stall = numberRow(QStringLiteral("agent/stall_timeout_s"), QStringLiteral("Stop a silent model after"),
                                                QStringLiteral("No output for this long ends the turn: retried once, then a message. "
                                                               "Reasoning models can be quiet for a while; 60 s is the default"),
                                                60, 1, 1800, QStringLiteral(" s"));
            stall.aliases = QStringLiteral("stall timeout hang stuck thinking silent retry");
            agent.rows << stall;
            // The wait for the *first* chunk is a different thing from the gaps between chunks:
            // prefill, queueing and routing on a prompt that may be hundreds of thousands of
            // tokens. A minute of silence in the middle of an answer is a dead stream; a minute
            // before it starts is an ordinary large prompt. 0 keeps both on the one number, which
            // is what Relay did before this row (a local endpoint has always had its own budget).
            relay::SettingRow firstToken =
                numberRow(QStringLiteral("agent/first_token_timeout_s"),
                          QStringLiteral("Wait longer for the first token"),
                          QStringLiteral("Extra patience before the model's first output, for a big "
                                         "prompt or a busy provider (0: use the limit above)"),
                          0, 0, 1800, QStringLiteral(" s"));
            firstToken.aliases = QStringLiteral("first token prefill timeout slow start deadline patience");
            alsoBoardWorkers(firstToken);
            agent.rows << firstToken;
        }
        sections << agent;

        // ----- Security (card #3KB7) -------------------------------------------------------
        // Owner, 2026-09-19: "add a security options menu with various secruity options like that
        // ... more of the approvals options on warp." Warp's execution profiles set a value per
        // capability — always_allow / always_ask / never — plus command and directory lists. Relay
        // keeps the two ends and not the middle: `docs/ROADMAP.md` settled against per-action
        // approvals, so where Warp asks, Relay denies, confines or bounds. The worker enforces
        // every row here (backend/relay_core/security.py); the rows only carry the lists to it.
        relay::SettingsSection security;
        security.id = QStringLiteral("security");
        security.title = QStringLiteral("Security");
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:security");
            info.label = QStringLiteral(
                "Relay allows by default: the agent's commands and file edits run without per-action "
                "approval — Ask before, below, is the opt-in that stops the actions you tick and asks first. "
                "What bounds them is where they may reach, and that is what this "
                "page sets. File tools are confined to the pane's workspace and refuse .ssh, .gnupg, .git, "
                ".env and .pem/.key files, on this machine and on an ssh host. Commands run with your own user "
                "permissions under a systemd memory limit — a denylist below is a guardrail against an obvious "
                "mistake, not a sandbox: a shell line can always be spelled another way.");
            security.rows << info;
        }
        // Moved here from Agent (card #3KB7): how far the agent's own hands reach is a bound,
        // and it belongs with the others. The chain limit (kMaxHandoffChain, src/InputPolicy.h)
        // is the backstop this row cannot turn off.
        {
            // run_in_terminal (protocol 22): how far a command the agent hands over may go. The
            // value is read when a turn starts, so it applies to the next prompt.
            const QString current = QSettings().value(QStringLiteral("agent/terminal_handoff"),
                                                      QStringLiteral("agent")).toString();
            security.rows << choiceRow(QStringLiteral("option:terminal_handoff"),
                                       QStringLiteral("Commands the agent hands to your terminal"),
                                       QStringLiteral("For ssh, sudo and logins, which the agent's own shell cannot run. "
                                                      "Three hand-offs in a row with nothing typed in between is the cap"),
                                       {QStringLiteral("agent"), QStringLiteral("prefill"), QStringLiteral("off")},
                                       {QStringLiteral("The agent runs it or puts it in the prompt box"),
                                        QStringLiteral("Always in the prompt box, for you to run"),
                                        QStringLiteral("Off")},
                                       current, QStringLiteral("agent"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/terminal_handoff"), value);
            });
        }
        security.rows << choiceRow(QStringLiteral("option:terminal_context"),
            QStringLiteral("Share terminal output with the agent"),
            QStringLiteral("Automatic attaches the latest command in this pane. Manual shares only output you attach. Off revokes reads. Independent of saved history."),
            {QStringLiteral("automatic"), QStringLiteral("manual"), QStringLiteral("off")},
            {QStringLiteral("Automatic"), QStringLiteral("Manual"), QStringLiteral("Off")},
            QSettings().value(QStringLiteral("agent/terminal_context"), QStringLiteral("automatic")).toString(),
            QStringLiteral("automatic"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/terminal_context"), value);
                relay::SettingsWatch::instance().notify();
            });
        security.rows << listRow(QStringLiteral("security/command_denylist"),
                                 QStringLiteral("Commands the agent never runs"),
                                 QStringLiteral("Comma-separated. A bare name is a program, so \"rm\" also refuses "
                                                "\"sudo rm\" and \"ls && rm x\" but not \"rmdir\"; a * makes it a "
                                                "pattern for the whole line (\"git push --force*\"). The agent is told "
                                                "which rule refused it."),
                                 QStringLiteral("rm, shutdown, git push --force*"),
                                 QStringLiteral(","),
                                 QStringLiteral("denylist deny block command"));
        security.rows << listRow(QStringLiteral("security/readable_roots"),
                                 QStringLiteral("Folders the agent may read outside the workspace"),
                                 QStringLiteral("Comma-separated absolute paths. Reading only — writing stays inside "
                                                "the workspace whatever is listed here, and the symlink and "
                                                "secret-file guards apply to these folders too."),
                                 QStringLiteral("/home/you/notes, /srv/reference"),
                                 QStringLiteral(","),
                                 QStringLiteral("directory allowlist folder read"));
        security.rows << listRow(QStringLiteral("security/secret_patterns"),
                                 QStringLiteral("Files the agent never reads"),
                                 QStringLiteral("Space-separated regular expressions, matched against each part of a "
                                                "path, added to the built-in list — which cannot be removed. Use \\s "
                                                "for a space."),
                                 QStringLiteral("\\.vault$ credentials"),
                                 QStringLiteral("\\s+"),
                                 QStringLiteral("secret redact pattern regex"));
        security.rows << toggleRow(QStringLiteral("security/clipboard_write"),
                                   QStringLiteral("Let the terminal put text on your clipboard"),
                                   QStringLiteral("OSC 52, off by default. Lets a command — including one the agent "
                                                  "runs — copy for you, and lets anything else that reaches the "
                                                  "screen replace what you are about to paste. Reading your "
                                                  "clipboard is never allowed and has no switch."), false);
        // Per-pane isolation (src/Isolation.h), moved here from Terminal (card #3KB7): the caps
        // scale with the machine (agent RAM/16 clamped to 2–8G, shell RAM/2 clamped to 4–16G),
        // and these rows write the same [isolation] keys relay.conf takes, so a manual edit and
        // this page agree.
        security.rows << headingRow(QStringLiteral("Memory limits"));
        security.rows << toggleRow(QStringLiteral("isolation/enabled"),
                                   QStringLiteral("Per-pane memory limits"),
                                   QStringLiteral("Each pane's shell and agent run in their own systemd scope, so a runaway "
                                                   "command stops inside its pane; applies to new panes"), true);
        {
            const QString current = QSettings().value(QStringLiteral("isolation/agent_memory_max"), QStringLiteral("auto")).toString();   // unset is "auto", so a fresh install shows no ↺
            relay::SettingRow row = choiceRow(QStringLiteral("option:agent_memory_max"),
                                              QStringLiteral("Agent memory limit"),
                                              QStringLiteral("Per pane, for the agent worker and the commands it runs; the next agent starts under it"),
                                              {QStringLiteral("auto"), QStringLiteral("2G"), QStringLiteral("4G"), QStringLiteral("8G"),
                                               QStringLiteral("16G"), QStringLiteral("infinity")},
                                              {QStringLiteral("Auto — %1 on this machine").arg(isolation::agentDefault()),
                                               QStringLiteral("2 GiB"), QStringLiteral("4 GiB"), QStringLiteral("8 GiB"),
                                               QStringLiteral("16 GiB"), QStringLiteral("No limit")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                if (value == QStringLiteral("auto")) QSettings().remove(QStringLiteral("isolation/agent_memory_max"));
                else QSettings().setValue(QStringLiteral("isolation/agent_memory_max"), value);
                // Card #Y4RX: the escapee cap below is this same limit, so rewrite its drop-ins.
                if (QSettings().value(QStringLiteral("isolation/cap_escapees"), false).toBool()) {
                    escapees::install(escapees::userConfigRoot(),
                                      {isolation::memory("isolation/agent_memory_max", isolation::agentDefault()),
                                       isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault())});
                    escapees::reload();
                }
            });
            row.aliases = QStringLiteral("oom memory isolation worker limit kill");
            security.rows << row;
        }
        {
            const QString current = QSettings().value(QStringLiteral("isolation/shell_memory_max"), QStringLiteral("auto")).toString();   // unset is "auto", so a fresh install shows no ↺
            relay::SettingRow row = choiceRow(QStringLiteral("option:shell_memory_max"),
                                              QStringLiteral("Shell memory limit"),
                                              QStringLiteral("Per pane, for the shell you type in; new panes start under it"),
                                              {QStringLiteral("auto"), QStringLiteral("4G"), QStringLiteral("8G"), QStringLiteral("16G"),
                                               QStringLiteral("32G"), QStringLiteral("infinity")},
                                              {QStringLiteral("Auto — %1 on this machine").arg(isolation::shellDefault()),
                                               QStringLiteral("4 GiB"), QStringLiteral("8 GiB"), QStringLiteral("16 GiB"),
                                               QStringLiteral("32 GiB"), QStringLiteral("No limit")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                if (value == QStringLiteral("auto")) QSettings().remove(QStringLiteral("isolation/shell_memory_max"));
                else QSettings().setValue(QStringLiteral("isolation/shell_memory_max"), value);
            });
            row.aliases = QStringLiteral("oom memory isolation shell limit kill");
            security.rows << row;
        }
        // The hole per-pane limits cannot close, and the owner's opt-in mitigation (card #Y4RX).
        // A child may ask the user's systemd for a transient scope of its own over D-Bus; the scope
        // it gets is a sibling of the pane's, not a child, so nothing Relay does from inside the
        // pane's scope contains it. tmux and Chrome both do exactly that.
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:escapee_scopes");
            info.label = QStringLiteral("Two programs get out from under these limits. tmux moves its server into "
                                        "tmux-spawn-<uuid>.scope and Chrome puts each app instance in "
                                        "app-com.google.Chrome-<pid>.scope, both directly under app.slice: a program "
                                        "may ask systemd for a scope of its own, and Relay cannot contain that from "
                                        "the pane's. Their memory counts against no pane's limit, and an out-of-memory "
                                        "kill in app.slice can land on any program there, not only the pane that "
                                        "started it.");
            security.rows << info;
        }
        {
            const QString cap = isolation::memory("isolation/agent_memory_max", isolation::agentDefault());
            const QString swap = isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault());
            relay::SettingRow row = toggleRow(QStringLiteral("isolation/cap_escapees"),
                                              QStringLiteral("Cap programs that leave their pane (tmux, Chrome)"),
                                              QStringLiteral("Off by default. Writes systemd user drop-ins that cap those two at "
                                                             "%1 of memory and %2 of swap — machine-wide for them, not per pane: "
                                                             "every tmux server and Chrome app scope on this machine, whether "
                                                             "Relay started it or not").arg(cap, swap),
                                              false, [this](bool on) {
                // Read now, not when the row was built: the limit above may have changed since.
                const QString cap = isolation::memory("isolation/agent_memory_max", isolation::agentDefault());
                const QString swap = isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault());
                const QString root = escapees::userConfigRoot();
                QStringList skipped;
                const QStringList touched = on ? escapees::install(root, {cap, swap}, &skipped)
                                               : escapees::removeAll(root, &skipped);
                const bool reloaded = (touched.isEmpty() && skipped.isEmpty()) || escapees::reload();
                QString said = on ? QStringLiteral("Capped tmux and Chrome at %1 (%2 drop-in(s) written)").arg(cap).arg(touched.size())
                                  : QStringLiteral("Removed Relay's tmux and Chrome caps (%1 file(s))").arg(touched.size());
                if (!skipped.isEmpty())
                    said += QStringLiteral("; left alone, not written by Relay: ") + skipped.join(QStringLiteral(", "));
                if (!reloaded) said += QStringLiteral("; `systemctl --user daemon-reload` failed, so it takes effect at your next login");
                statusBar()->showMessage(said + QStringLiteral("."), 8000);
            });
            row.aliases = QStringLiteral("tmux chrome browser scope app.slice oom escape dropin systemd cap");
            security.rows << row;
        }
        // Turn bounds, moved here from Agent (card #3KB7): they are the real cost and runaway
        // control. The compaction threshold and the two timeouts stay on the Agent page, where
        // they describe the model call rather than what it may reach.
        security.rows << headingRow(QStringLiteral("Turn bounds"));
        security.rows << numberRow(QStringLiteral("agent/max_auto_turns"),
                                   QStringLiteral("Automatic turns from background agents"),
                                   QStringLiteral("In a row without your input (0 = unlimited)"), 50, 0, 10000);
        // Card #2CZP: both sit at their maxima by default, so a long overnight run is not stopped by
        // a count. A turn that has stopped making progress is ended by the loop detector instead;
        // these two are the fuse behind it, for a runaway turn nothing else catches.
        {
            auto limit = numberRow(QStringLiteral("agent/max_steps"), QStringLiteral("Step limit per turn"),
                                   QStringLiteral("Backstop for a runaway turn; then it stops with Continue"), 500, 1, 500);
            alsoBoardWorkers(limit);
            security.rows << limit;
        }
        {
            auto limit = numberRow(QStringLiteral("agent/max_tool_calls"), QStringLiteral("Tool-call limit per turn"),
                                   QStringLiteral("Backstop for a runaway turn, not the normal stop"), 2000, 1, 2000);
            alsoBoardWorkers(limit);
            security.rows << limit;
        }
        security.rows << toggleRow(QStringLiteral("agent/audit_requests"), QStringLiteral("Audit requests after each turn"),
                                   QStringLiteral("A small side call flags asks that may be unaddressed"), false);
        // Card #K2FV: the opt-in ask. Seven rows, one saved list; an approval ask's "Always
        // allow" unticks the matching row by writing the same list. The labels are the ask's
        // headers (approvals.LABELS), so the row an ask names is the row that unticks.
        security.rows << headingRow(QStringLiteral("Ask before"));
        security.rows << approvalRow(QStringLiteral("edit"), QStringLiteral("Change a file that already exists"),
                                     QStringLiteral("edit_file and write_file, on a file that is there"));
        security.rows << approvalRow(QStringLiteral("create"), QStringLiteral("Create a new file"),
                                     QStringLiteral("write_file, where no file is yet"));
        security.rows << approvalRow(QStringLiteral("delete_or_move"), QStringLiteral("Delete or move files"),
                                     QStringLiteral("rm, mv and the like, read from the command line — a script or a "
                                                    "variable can still spell them another way"));
        security.rows << approvalRow(QStringLiteral("read_outside"), QStringLiteral("Read outside the workspace"),
                                     QStringLiteral("read_file and list_directory outside the pane's workspace; the "
                                                    "readable-folders list above widens what counts as inside"));
        security.rows << approvalRow(QStringLiteral("terminal"), QStringLiteral("Run in your terminal"),
                                     QStringLiteral("run_in_terminal: the command runs in your own shell, not the "
                                                    "agent's"));
        security.rows << approvalRow(QStringLiteral("program"), QStringLiteral("Type into your program"),
                                     QStringLiteral("type_into_program, on a program you have handed over"));
        security.rows << approvalRow(QStringLiteral("network"), QStringLiteral("Reach the network"),
                                     QStringLiteral("curl, wget, git push and the like, read from the command line"));
        security.rows << buttonRow(QStringLiteral("option:approvals/again"), QStringLiteral("The first-launch choice"),
                                   QStringLiteral("Cautious by default, or allow everything — shown on the first "
                                                  "configure until it is answered"),
                                   QStringLiteral("Show it again"), [this] {
            QSettings().remove(QStringLiteral("security/approvals_chosen"));
            if (m_active) {
                m_active->agentOptionsChanged(QStringLiteral("security/approvals_ask"));
                openApprovalsPane(m_active);
            }
        });
        sections << security;


        // Voice transcription (issue NY7Z). The section names the model and says where the audio
        // goes, because that is the one thing a microphone button must not leave implicit.
        relay::SettingsSection voice;
        voice.id = QStringLiteral("voice");
        voice.title = QStringLiteral("Voice");
        voice.blurb = QStringLiteral("Hold the voice key or click the microphone in the prompt strip, speak, and the "
                                     "transcript is inserted into the prompt box. Recordings are sent to OpenRouter "
                                     "(and from there to the model's provider) and are deleted as soon as they come "
                                     "back as text; voice needs an OpenRouter key whatever model your panes use.");
        voice.rows << headingRow(QStringLiteral("Capture"));
        voice.rows << toggleRow(QStringLiteral("voice/enabled"), QStringLiteral("Voice transcription"),
                                QStringLiteral("The microphone chip and the voice key"), true);
        {
            QStringList ids = relay::voice::holdKeys(), labels;
            for (const QString &id : ids) labels << relay::voice::holdKeyLabel(id);
            // No fixed default: the key Relay starts on is read off the keyboard layout the first
            // time (Pane::voiceHoldKey), so putting it back means forgetting what was stored and
            // letting it be derived again — on this machine's layout, which may have changed.
            relay::SettingRow hold = choiceRow(QStringLiteral("option:voice_hold_key"), QStringLiteral("Voice key"),
                                    QStringLiteral("Held down while you speak; released, it transcribes"),
                                    ids, labels, Pane::voiceHoldKey(), QString(), [this](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/hold_key"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("voice/hold_key"));
            });
            hold.reset = [this] {
                QSettings().remove(QStringLiteral("voice/hold_key"));
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("voice/hold_key"));
            };
            // Derived rather than fixed, so "changed" is whether a key was ever chosen by hand.
            hold.changed = QSettings().contains(QStringLiteral("voice/hold_key"));
            voice.rows << hold;
        }
        voice.rows << numberRow(QStringLiteral("voice/max_seconds"), QStringLiteral("Longest recording"),
                                QStringLiteral("Recording stops by itself after this many seconds"),
                                relay::voice::kDefaultSeconds, 5, 600, QStringLiteral(" s"));
        voice.rows << textRow(QStringLiteral("voice/device"), QStringLiteral("Microphone"),
                              QStringLiteral("The capture tool's own source name (empty: the desktop default)"),
                              QStringLiteral("default"));
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("option:voice_recorder");
            const QString tool = relay::voice::chooseTool(QSettings().value(QStringLiteral("voice/tool")).toString(),
                                                          relay::voice::toolOnPath);
            // An Info row renders its label only, so the whole sentence goes there.
            info.label = tool.isEmpty()
                ? relay::voice::missingToolsMessage()
                : QStringLiteral("Recorder: %1 · 16 kHz mono WAV in a temporary file, deleted once it comes back "
                                 "as text.").arg(tool);
            voice.rows << info;
        }
        voice.rows << headingRow(QStringLiteral("Model"));
        {
            // The three that were live-tested (issue NY7Z); the ids match backend/relay_core/voice.py.
            // The first is the one Relay ships on, here and in Pane::voiceModel().
            const QStringList ids{QStringLiteral("google/gemini-3.5-flash-lite"), QStringLiteral("google/gemini-3.8-flash"),
                                  QStringLiteral("openai/whisper-1")};
            const QStringList labels{QStringLiteral("Gemini 3.5 Flash-Lite — fastest, ~$0.00006 a clip"),
                                     QStringLiteral("Gemini 3.8 Flash — most accurate, ~$0.0004 a clip"),
                                     QStringLiteral("Whisper — transcription endpoint, ~$0.0003 a clip")};
            voice.rows << choiceRow(QStringLiteral("option:voice_model"), QStringLiteral("Transcription model"),
                                    QStringLiteral("Runs on OpenRouter with your OpenRouter key"),
                                    ids, labels, Pane::voiceModel(), ids.constFirst(), [](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/model"), value);
            });
        }
        voice.rows << buttonRow(QStringLiteral("agent.modelKeys"), QStringLiteral("OpenRouter key"),
                                QStringLiteral("Voice needs one of its own, whatever model your panes run"),
                                QStringLiteral("API keys…"), [this] { runAction(QStringLiteral("agent.modelKeys")); });
        // Read aloud (#MDA7): the system's own voice and rate, so there is no voice picker here.
        voice.rows << headingRow(QStringLiteral("Read aloud"));
        voice.rows << toggleRow(QStringLiteral("speech/auto_read"), QStringLiteral("Read replies aloud automatically"),
                                QStringLiteral("Each finished agent reply, in the system voice; Esc stops it and /speak "
                                               "reads the last one again"), false);
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("option:speech_engine");
            const QString engine = relay::speech::Speaker::instance().engine();
            info.label = engine.isEmpty() ? relay::speech::missingToolsMessage()
                                          : QStringLiteral("Speaker: %1 · the system's default voice and rate.").arg(engine);
            voice.rows << info;
        }
        sections << voice;

        // Remote (#PH0N): one switch and the address it uses. The page is built where the
        // settings live (src/RemoteSettings.cpp), so a test can read its rows without a window.
        sections << remoteSection();

        relay::SettingsSection privacy;
        privacy.id = QStringLiteral("privacy");
        privacy.title = QStringLiteral("Privacy");
        privacy.blurb = QStringLiteral("Relay has no telemetry. Everything below decides what leaves this machine. "
                                       "On your own key it goes to that provider only; on Relay Free it goes through "
                                       "Relay's hosted service to the provider, and Relay keeps request metadata only.");
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:privacy");
            info.label = QStringLiteral(
                "Keys are stored in the desktop keyring (secret-tool, service org.relayterminal.Relay) or read "
                "from RELAY_<PROVIDER>_API_KEY. They are never written to Relay's settings files and never logged. "
                "A request on your own key never touches Relay's server. On Relay Free, the pane's prompts and "
                "tool context go to Relay's hosted service and on to the model provider; Relay logs request "
                "metadata (time, size, outcome) and never the text. Pick another provider to switch it off. "
                "Terminal history is not sent automatically. Shell commands the agent runs are NOT sandboxed: they "
                "have your user permissions. File tools are restricted to the agent workspace.");
            privacy.rows << info;
        }
        privacy.rows << toggleRow(QStringLiteral("suggestions/next_command"),
                                  QStringLiteral("AI next-command suggestions"),
                                  QStringLiteral("After a command finishes. Sends the command and its recent output "
                                                 "to the Suggestions model, which stays on your own provider."), true);
        privacy.rows << toggleRow(QStringLiteral("suggestions/next_prompt"),
                                  QStringLiteral("Suggested next prompts"),
                                  QStringLiteral("After an agent turn. Sends a summary of the conversation."), true);
        privacy.rows << toggleRow(QStringLiteral("instructions/project_auto"),
                                  QStringLiteral("Load project instruction files automatically"),
                                  QStringLiteral("CLAUDE.md, AGENTS.md and WARP.md found in the workspace"), true);
        {
            // Review B1 (protocol 26.7): the Sessions list reads Claude Code's and Codex's own
            // transcripts into Relay's index so they can be listed, searched and resumed. The
            // copy never leaves this machine, but it is a copy of every prompt and reply, and
            // until this row there was no way to say no.
            relay::SettingRow row = toggleRow(QStringLiteral("sessions/index_guests"),
                                  QStringLiteral("List Claude Code and Codex sessions"),
                                  QStringLiteral("Relay copies their prompts and replies into its own index (on this "
                                                 "machine only) so Sessions can search and resume them. Off removes "
                                                 "the copies; their own files are never touched"), true);
            row.aliases = QStringLiteral("claude codex guest sessions index transcripts history privacy search");
            privacy.rows << row;
        }
        {
            // #MEMS (owner, 2026-09-22: "guests use relay memory, and thats the default"). Read by
            // relay_core.guest_launch and the harness route (`guest.memory`) at the next start;
            // their own ~/.claude and ~/.codex are never edited for it.
            const QString key = QStringLiteral("guests/memory");
            const QString current = QSettings().value(key).toString().trimmed();
            relay::SettingRow row = choiceRow(QStringLiteral("option:") + key,
                QStringLiteral("Guests use memory from"),
                QStringLiteral("Claude Code and Codex started by Relay. Relay: their own memory is off and they see "
                               "Relay's user memory, suggesting new facts for you to keep. Both adds Relay's to theirs. "
                               "Applies from the guest's next start"),
                {QStringLiteral("relay"), QStringLiteral("own"), QStringLiteral("both")},
                {QStringLiteral("relay"), QStringLiteral("their own"), QStringLiteral("both")},
                current.isEmpty() ? QStringLiteral("relay") : current, QStringLiteral("relay"),
                [this, key](const QString &value) {
                    if (value.isEmpty() || value == QStringLiteral("relay")) QSettings().remove(key);
                    else QSettings().setValue(key, value);
                    refreshSettingsPanes();
                });
            row.aliases = QStringLiteral("claude codex guest memory remember auto-memory memories user facts");
            privacy.rows << row;
        }
        // #MEMS, protocol 34: read by every worker's `configure`, so it applies from the next start.
        privacy.rows << toggleRow(QStringLiteral("memory/import_guests"), QStringLiteral("Offer Claude Code and Codex memories"),
                                  QStringLiteral("At start, facts about you in ~/.claude and ~/.codex become suggestions in "
                                                 "Globals › Suggestions; nothing is remembered until you keep it"), true);
        sections << privacy;

        relay::SettingsSection shortcuts;
        shortcuts.id = QStringLiteral("keyboard");
        shortcuts.title = QStringLiteral("Keyboard");
        shortcuts.blurb = QStringLiteral("Keys are read from keybindings.json, and your own overrides sit on top of the "
                                         "preset. Every action and the keys it answers to: %1.")
                              .arg(Keymap::instance().shortcutText(QStringLiteral("help.shortcuts")));
        {
            const QString presetId = Keymap::instance().preset();
            QStringList values, labels;
            for (const auto &preset : Keymap::presets()) { values << preset.first; labels << preset.second; }
            shortcuts.rows << choiceRow(QStringLiteral("option:keymap_preset"), QStringLiteral("Shortcut preset"),
                                        Keymap::instance().hasOverrides()
                                            ? QStringLiteral("Your custom overrides stay on top")
                                            : QStringLiteral("Starting point for every shortcut"),
                                        values, labels, presetId, QStringLiteral("relay"),
                                        [](const QString &id) { Keymap::instance().setPreset(id); });
        }
        {
            const QString programKeys = Keymap::instance().programKeys();
            shortcuts.rows << choiceRow(QStringLiteral("option:program_keys"), QStringLiteral("Shortcuts inside programs"),
                                        QStringLiteral("Which Relay keys still act while vim, nano or less runs"),
                                        {QStringLiteral("shift-only"), QStringLiteral("all"), QStringLiteral("none")},
                                        {QStringLiteral("Ctrl+Shift and F-keys only"),
                                         QStringLiteral("All shortcuts act"),
                                         QStringLiteral("Programs get every key")},
                                        programKeys, QStringLiteral("shift-only"),
                                        [](const QString &value) { Keymap::instance().setProgramKeys(value); });
        }
        // Like API keys and Instructions, this opens the editor of something that persists, so it
        // stays an option (and is an action too). Reloading the file is only an action.
        shortcuts.rows << buttonRow(QStringLiteral("keybindings.edit"), QStringLiteral("Edit keyboard shortcuts"),
                                    QStringLiteral("Opens keybindings.json in your editor"),
                                    QStringLiteral("Edit…"), [this] { runAction(QStringLiteral("keybindings.edit")); });
        {
            // What the mouse does, which no keybinding can say (owner, 2026-09-18). These are not
            // actions with keys, so they are one informational row here rather than rows in a list
            // that offers to run them.
            relay::SettingRow mouse;
            mouse.kind = relay::SettingRow::Info;
            mouse.id = QStringLiteral("info:mouse");
            mouse.aliases = QStringLiteral("mouse drag click gestures pointer header grip rename explorer");
            mouse.label = QStringLiteral(
                "With the mouse: drag a pane's header — or the ⠿ grip on an explorer, preview or Board "
                "pane — onto another pane's edge to move it there; onto a tab's label to move it into that "
                "tab; or onto the tab bar's empty space to give it a tab of its own. Esc during the drag "
                "puts it back. Double click a pane's title to rename it. Click the "
                "folder line on the right of the header to open that folder in an explorer pane, and again to "
                "close it. Ctrl+click a path, a URL or a “tool calls” line in the terminal to open it. Right "
                "click in the terminal for Relay's menu.");
            shortcuts.rows << mouse;
        }
        sections << shortcuts;

        // ----- About (owner, 2026-09-20: "i think there should be an about section in the
        // options") -------------------------------------------------------------------------
        // What this Relay *is*, which is what you go looking for when you want to say which build
        // you saw something in. The build line lived under General › Diagnostics, where nobody
        // thought to look for it; Diagnostics keeps what you *set* (the log level), and this page
        // holds what you *read*. Nothing here is a setting, so the page gets no "Reset to
        // defaults" row — resetRow() builds that from rows that declare a default, and none of
        // these do.
        relay::SettingsSection about;
        about.id = QStringLiteral("about");
        about.title = QStringLiteral("About");
        about.blurb = QStringLiteral("Which Relay this is. The Copy button below puts all of it on the clipboard in "
                                     "one block, which is what a bug report wants.");
        {
            relay::SettingRow identity;
            identity.kind = relay::SettingRow::Info;
            identity.id = QStringLiteral("info:about.identity");
            identity.aliases = QStringLiteral("about version licence license agpl free software source");
            identity.label = QStringLiteral("Relay %1 — a terminal whose panes have their own agents.\n"
                                            "Free software under the GNU Affero General Public License, version 3 or "
                                            "later (the LICENSE file beside the source says it in full).")
                                 .arg(QStringLiteral(RELAY_VERSION));
            about.rows << identity;
        }
        {
            // Which build this window is running, and whether the one on disk has moved on (owner,
            // 2026-09-19). A rebuild never reaches a running Relay, nor the windows it opens.
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            const QString onDisk = relay::buildinfo::idOnDisk();
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:build");
            info.aliases = QStringLiteral("about build number version running since binary path rebuild");
            info.label = QStringLiteral("Build %1 · version %2 · running since %3 · %4")
                             .arg(build.id, QStringLiteral(RELAY_VERSION), build.started.toString(QStringLiteral("HH:mm")),
                                  QCoreApplication::applicationFilePath());
            if (onDisk != build.id)
                info.label += QStringLiteral("\nA newer build is on disk: %1. Quit and reopen Relay to run it; New window "
                                             "stays on this one, a launch from the taskbar starts the new one.").arg(onDisk);
            about.rows << info;
        }
        {
            relay::SettingRow parts;
            parts.kind = relay::SettingRow::Info;
            parts.id = QStringLiteral("info:about.parts");
            parts.aliases = QStringLiteral("about engine core libvterm ghostty qt platform kernel architecture");
            // defaultEngineCore() is empty unless --engine-core or RELAY_ENGINE_CORE named one, so
            // it is the *core* rather than the engine; say it the way the pane header already does.
            const QString core = relay::defaultEngineCore();
            parts.label = QStringLiteral("Terminal engine: %1 · Qt %2 · %3 · %4")
                              .arg(core.isEmpty() ? QStringLiteral("Relay engine")
                                                  : QStringLiteral("Relay engine (%1)").arg(core),
                                   QString::fromLatin1(qVersion()),
                                   QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
            about.rows << parts;
        }
        about.rows << buttonRow(QStringLiteral("about.copy"), QStringLiteral("Copy this page"),
                                QStringLiteral("Version, build, engine and platform, as one block to paste into a bug report"),
                                QStringLiteral("Copy"), [this] {
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            const QString onDisk = relay::buildinfo::idOnDisk();
            QStringList block;
            block << QStringLiteral("Relay %1").arg(QStringLiteral(RELAY_VERSION))
                  << QStringLiteral("build: %1").arg(build.id)
                  << QStringLiteral("binary: %1").arg(QCoreApplication::applicationFilePath())
                  << QStringLiteral("engine: %1").arg(relay::defaultEngineCore().isEmpty()
                                                          ? QStringLiteral("relay")
                                                          : QStringLiteral("relay/%1").arg(relay::defaultEngineCore()))
                  << QStringLiteral("qt: %1").arg(QString::fromLatin1(qVersion()))
                  << QStringLiteral("system: %1 (%2)").arg(QSysInfo::prettyProductName(),
                                                           QSysInfo::currentCpuArchitecture());
            if (onDisk != build.id) block << QStringLiteral("build on disk: %1").arg(onDisk);
            QGuiApplication::clipboard()->setText(block.join(QLatin1Char('\n')));
            notice(QStringLiteral("Copied: Relay %1, build %2.").arg(QStringLiteral(RELAY_VERSION), build.id), 5000);
        });
        sections << about;

        // Last on every page: a way to put that page back to what Relay ships with (owner,
        // 2026-09-18). It is built from each section's own rows, so it reaches exactly the options
        // you are looking at and nothing on another tab, and a page where nothing declares a
        // default — Local models, whose rows are the servers you saved — gets no button at all.
        for (relay::SettingsSection &section : sections) {
            relay::SettingRow reset = relay::resetRow(section, [this, title = section.title](int count) {
                // Queued: this runs from the button's own click, and refreshing the panes rebuilds
                // the row the button sits in.
                QTimer::singleShot(0, this, [this, title, count] {
                    notice(QStringLiteral("%1: %2 %3 back to Relay's defaults.")
                               .arg(title).arg(count)
                               .arg(count == 1 ? QStringLiteral("option is") : QStringLiteral("options are")), 6000);
                    // The theme, the log level and the keymap are already in effect; the other open
                    // Options panes are still drawing the values that have just gone.
                    refreshSettingsPanes();
                });
            });
            if (!reset.id.isEmpty()) section.rows << reset;
        }
        return sections;
    }
