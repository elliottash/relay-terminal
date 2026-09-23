// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"
#include "WindowManagerImpl.h"

relay::SettingsSection RelayWindow::modelsSection(bool inModelsPane) {
        relay::SettingsSection models;
        models.id = QStringLiteral("models");
        models.title = QStringLiteral("Models");
        models.blurb = QStringLiteral("Relay runs on the models you already pay for: an API key, a coding-plan subscription "
                                      "(GLM, Kimi, MiniMax), or your Claude Code and Codex logins. Keys live in the "
                                      "desktop keyring and your own providers receive requests directly. Relay Free and "
                                      "Relay Pro send prompts through "
                                      "Relay's hosted service. A profile names the five lists as a set, so \"AI work\" "
                                      "and \"admin work\" can rank models differently and swap in one switch (/profile). "
                                      "The lists themselves are the models pane: Ctrl+Shift+M, /model "
                                      "or /models opens it — providers, available models, priorities and "
                                      "jobs, four tabs — and Alt+M drops the pane's model box open.");
        Pane *pane = m_active;
        QSettings settings;
        QWidget *page = m_tabs->currentWidget();
        // The providers page must use the same worker as Available and Priorities. The active
        // terminal can change while this Models pane continues to serve its original target.
        if (inModelsPane)
            if (auto *view = modelsViewOf(modelsPaneIn(page)))
                if (Pane *served = findPaneByToken(view->servedToken())) pane = served;
        // The pane's worker answers when a pane's agent is up. Otherwise the tab's helper agent
        // does (owner, 2026-09-20: "if there is no agent loaded yet, load the helper agent"): it
        // is started on the first look at this page, asked for `presets`, and every request the
        // page makes goes down its pipe instead.
        const bool viaHelper = !pane || pane->allPresets().isEmpty();
        QJsonArray presets = viaHelper ? m_helperPresets.value(tabIdOf(page)) : pane->allPresets();
        if (viaHelper && presets.isEmpty())
            if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send({{"type", "presets"}});
        auto request = [this, pane, viaHelper, page](const QJsonObject &message) {
            if (!viaHelper && pane) { pane->sendModelRequest(message); return; }
            if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send(message);
        };
        // The pane's catalog, not a bare catalogFrom: it carries the usage limits that arrived
        // since the last presets answer (the "5h 62% left" status line, an exhausted row).
        const relay::models::Catalog catalog = !viaHelper ? pane->modelCatalog() : relay::models::catalogFrom(presets);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        auto curated = [this] { modelsCurated(); };
        auto str = [](const QJsonObject &object, const char *field) { return object.value(QLatin1String(field)).toString(); };

        // ----- 0. the way to the dialog -----------------------------------------------------------
        // The five lists and the "models in the picker" checklist were sections 2 and 4 of this
        // page — owner, 2026-09-21: "the model priority chooser is crtical, and currently its too
        // hard to find -- model options, then scroll down." They are the Ctrl+Alt+M dialog now
        // (card #MDL1 t:a7 and t:a10, design 5.5) and this row is the door to it. What is left on
        // the page is what the dialog is not about: providers, their keys, and the profiles that
        // name a set of lists. It opens on main, because a page is not in a mode the way a pane is.
        if (!inModelsPane) {
            const QString chord = Keymap::instance().shortcutText(QStringLiteral("agent.modelOptions"));
            relay::SettingRow row = buttonRow(QStringLiteral("models.prioritize"),
                QStringLiteral("models and priorities"),
                QStringLiteral("Every model and the five lists, in the models pane beside this one: a tab per list, "
                               "enter to use a model in the pane it serves, alt+↑↓ or a drag to reorder, delete to "
                               "take one out, typing to find any model — OpenRouter's long tail included — and "
                               "ctrl+enter to add it"),
                chord.isEmpty() ? QStringLiteral("models and priorities…") : QStringLiteral("models and priorities… (%1)").arg(chord),
                [this] {
                    // Not runAction("agent.modelOptions"), which opens on the pane's own mode and
                    // toggles: asked from this page, the main list is the one meant.
                    Pane *on = m_active ? m_active.data() : focusedConsole();
                    if (on) on->openModelPicker(QStringLiteral("main"));
                    else notice(QStringLiteral("Focus a pane first: the models pane always serves one."));
                });
            row.aliases = QStringLiteral("prioritize priority order rank tier lists main list flash lite high local "
                                         "models pane picker checklist shown add a model by id ctrl+shift+m");
            models.rows << row;
        }

        // ----- 1. providers ---------------------------------------------------------------------
        // Owner (2026-09-20): only the providers you can use are listed — a key stored, Relay Free,
        // Claude Code and Codex on this machine — plus OpenRouter always, because it is the one key
        // the fallbacks and the Lite tier lean on. The rest wait behind "+ add provider". Listed
        // providers keep the order they were added in, and drag to reorder.
        {
            // Folds by default once a provider is set up (owner, 2026-09-20): from then on the
            // page opens on the models, and the keys are one click away.
            //
            // **Not in the models pane** (card #MDL1 t:a11). There the providers rows are the whole
            // of a tab called "providers", and a fold — remembered under the same
            // `options/collapsed/heading:providers` key, so one set up on Options travelled here —
            // left a first-run window showing the blurb, the profiles and the defaults and not the
            // one thing step 1 is about. A first run opens on this tab precisely because there is a
            // key to add, so the group is drawn open and has no fold control at all.
            relay::SettingRow head = headingRow(QStringLiteral("providers"));
            head.collapsible = !inModelsPane;
            bool anySetUp = false;
            for (const auto &value : presets) {
                const QJsonObject preset = value.toObject();
                anySetUp = anySetUp || preset.value(QStringLiteral("has_stored_key")).toBool() || preset.value(QStringLiteral("custom")).toBool()
                    || (preset.value(QStringLiteral("harness")).toBool() && preset.value(QStringLiteral("logged_in")).toBool());
            }
            head.collapsedByDefault = anySetUp && !inModelsPane;
            models.rows << head;
        }
        if (presets.isEmpty()) {
            relay::SettingRow none;
            none.kind = relay::SettingRow::Info;
            none.id = QStringLiteral("info:models/none");
            none.label = QStringLiteral("Starting the helper agent to read your providers…");
            models.rows << none;
        }
        // The add-key flow, shared by the listed rows and "+ add provider".
        auto askForKey = [this, request](const QString &id, const QString &label) {
            bool ok = false;
            // Password echo: the key is never rendered and never leaves this call.
            //
            // The two sentences after the first are the ones the retired "Advanced provider
            // settings" dialog put behind a consent checkbox (card #MDL1, 2026-09-21: "the consent
            // sentence about tools running without asking moves to wherever the first key is
            // entered if it is not already said there"). This is where a key is entered, so this
            // is where they are said. Not a checkbox: Relay has no per-action tool approvals by
            // ruling (WARP.md), so a tick that only said "yes, run tools" would gate nothing and
            // make the person agree to something they cannot decline and keep an agent.
            const QString key = QInputDialog::getText(this, QStringLiteral("API key"),
                QStringLiteral("Key for %1.\n"
                               "It is saved to the desktop keyring and sent only to this provider — never to "
                               "Relay's server, and never written to a settings file.\n\n"
                               "Your prompts and tool results go to this provider when the agent runs. The agent "
                               "runs tools without asking: shell commands are not sandboxed and have your own "
                               "permissions, and file tools are held to the pane's directory.").arg(label),
                QLineEdit::Password, QString(), &ok).trimmed();
            if (!ok || key.isEmpty()) return;
            if (key.contains(QRegularExpression(QStringLiteral("\\s")))) {
                QMessageBox::warning(this, QStringLiteral("API key"), QStringLiteral("An API key cannot contain spaces."));
                return;
            }
            request({{"type", "store_key"}, {"preset", id}, {"api_key", key}});
        };
        // The custom-endpoint form (owner, 2026-09-20: "like in warp custom providers"): a name,
        // an OpenAI-compatible base URL, a key, the model ids. Saved through the pane's worker.
        auto askForCustom = [this, request](const QJsonObject &existing) {
            QDialog dialog(this);
            dialog.setWindowTitle(existing.isEmpty() ? QStringLiteral("custom provider") : QStringLiteral("edit custom provider"));
            auto *form = new QFormLayout(&dialog);
            auto *name = new QLineEdit(existing.value(QStringLiteral("name")).toString());
            name->setPlaceholderText(QStringLiteral("my proxy"));
            auto *url = new QLineEdit(existing.value(QStringLiteral("base_url")).toString());
            url->setPlaceholderText(QStringLiteral("https://host/v1  (OpenAI-compatible; /chat/completions is appended)"));
            auto *key = new QLineEdit;
            key->setEchoMode(QLineEdit::Password);
            key->setPlaceholderText(existing.isEmpty() ? QStringLiteral("API key (optional for a loopback URL)")
                                                       : QStringLiteral("leave empty to keep the stored key"));
            QStringList ids;
            for (const auto &item : existing.value(QStringLiteral("model_ids")).toArray()) ids << item.toString();
            auto *models = new QLineEdit(ids.join(QStringLiteral(", ")));
            models->setPlaceholderText(QStringLiteral("model ids, comma-separated; the first is the default"));
            auto *effort = new QComboBox;
            effort->addItem(QStringLiteral("none · send no reasoning setting"), QStringLiteral("none"));
            effort->addItem(QStringLiteral("openrouter · reasoning.effort"), QStringLiteral("openrouter"));
            effort->addItem(QStringLiteral("kimi · reasoning_effort"), QStringLiteral("kimi"));
            effort->setCurrentIndex(qMax(0, effort->findData(existing.value(QStringLiteral("effort_style")).toString(QStringLiteral("none")))));
            auto *extra = new QPlainTextEdit;
            extra->setObjectName(QStringLiteral("customProviderExtra"));
            extra->setPlaceholderText(QStringLiteral("Optional JSON object, e.g. {\"temperature\": 0.7}\nEmpty clears extra parameters."));
            extra->setMaximumHeight(130);
            const QJsonObject savedExtra = existing.value(QStringLiteral("extra")).toObject();
            if (!savedExtra.isEmpty()) extra->setPlainText(QString::fromUtf8(QJsonDocument(savedExtra).toJson(QJsonDocument::Indented)));
            form->addRow(QStringLiteral("name"), name);
            form->addRow(QStringLiteral("base url"), url);
            form->addRow(QStringLiteral("api key"), key);
            form->addRow(QStringLiteral("models"), models);
            form->addRow(QStringLiteral("reasoning"), effort);
            form->addRow(QStringLiteral("extra request JSON"), extra);
            auto *error = new QLabel;
            error->setObjectName(QStringLiteral("customProviderExtraError"));
            error->setWordWrap(true);
            error->hide();
            form->addRow(error);
            QJsonObject extraObject;
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
                const QByteArray input = extra->toPlainText().trimmed().toUtf8();
                QJsonParseError parseError;
                const QJsonDocument parsed = QJsonDocument::fromJson(input.isEmpty() ? QByteArray("{}") : input, &parseError);
                QString message;
                if (parseError.error != QJsonParseError::NoError)
                    message = QStringLiteral("Extra request JSON: %1 (offset %2). Enter a JSON object or leave empty.")
                        .arg(parseError.errorString()).arg(parseError.offset);
                else if (!parsed.isObject())
                    message = QStringLiteral("Extra request JSON must be an object, for example {\"temperature\": 0.7}.");
                else {
                    const QStringList allowed{QStringLiteral("thinking"), QStringLiteral("reasoning"),
                        QStringLiteral("reasoning_effort"), QStringLiteral("temperature"), QStringLiteral("top_p")};
                    for (const QString &field : parsed.object().keys()) {
                        if (!allowed.contains(field)) {
                            message = QStringLiteral("Unsupported extra parameter '%1'. Allowed: %2.").arg(field, allowed.join(QStringLiteral(", ")));
                            break;
                        }
                    }
                }
                if (!message.isEmpty()) {
                    error->setText(message);
                    error->show();
                    extra->setFocus();
                    return;
                }
                extraObject = parsed.object();
                dialog.accept();
            });
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            dialog.resize(560, dialog.sizeHint().height());
            if (dialog.exec() != QDialog::Accepted) return;
            QJsonObject provider{{QStringLiteral("name"), name->text().trimmed()},
                                 {QStringLiteral("base_url"), url->text().trimmed()},
                                 {QStringLiteral("models"), models->text().trimmed()},
                                 {QStringLiteral("effort_style"), effort->currentData().toString()},
                                 {QStringLiteral("extra"), extraObject}};
            if (!existing.isEmpty()) provider.insert(QStringLiteral("id"), existing.value(QStringLiteral("id")).toString());
            if (!key->text().trimmed().isEmpty()) provider.insert(QStringLiteral("api_key"), key->text().trimmed());
            request({{"type", "custom_provider_save"}, {"provider", provider}});
        };
        QList<QJsonObject> listed, waiting;
        for (const auto &value : presets) {
            const QJsonObject preset = value.toObject();
            const QString id = str(preset, "id");
            if (id.isEmpty() || preset.value(QStringLiteral("local")).toBool()) continue;   // Options › Local models
            // Relay Free is not a provider you set up (owner, 2026-09-20: "don't show relay free in
            // the providers list"): it has no key, no login and nothing to test into. Its models
            // still sit in the checklist and the priority list like any other.
            if (id == QStringLiteral("relay-free")) continue;
            const bool guest = id.startsWith(QStringLiteral("guest:"));
            const bool usable = preset.value(QStringLiteral("has_stored_key")).toBool()
                || preset.value(QStringLiteral("custom")).toBool()
                || (preset.value(QStringLiteral("hosted")).toBool() && preset.value(QStringLiteral("available")).toBool())
                || (guest && preset.value(QStringLiteral("installed")).toBool(true));
            (usable || id == QStringLiteral("openrouter") || id == QStringLiteral("relay-pro") ? listed : waiting) << preset;
        }
        // Order of addition, then whatever you dragged (owner, 2026-09-20): the first time a
        // provider is listed is its place, and a drop moves it.
        {
            QStringList ids;
            for (const QJsonObject &preset : std::as_const(listed)) ids << str(preset, "id");
            relay::models::curation::noteProviders(ids);
            const QStringList order = relay::models::curation::providerOrder();
            std::stable_sort(listed.begin(), listed.end(), [&](const QJsonObject &a, const QJsonObject &b) {
                return order.indexOf(str(a, "id")) < order.indexOf(str(b, "id"));
            });
        }
        // Read once for the whole loop, not once per entry: every `isAvailable` would otherwise be
        // its own QSettings lookup (the same trap as `shown`, #PPR4).
        const QStringList availableKeys = relay::models::curation::availableKeys();
        // A provider is three rows on a good day — its key, its "models… (N of M)" link and, for a
        // guest, what it may do with a tool — and they ran into the next provider's as one wall of
        // text (owner, 2026-09-21: "tab 1: add horizontal line dividers between providers"). The
        // rule goes on each provider's own row but the first's, in the theme's `@border`, which is
        // the colour a section heading is underlined in.
        bool firstProvider = true;
        for (const QJsonObject &preset : std::as_const(listed)) {
            const QString id = str(preset, "id");
            const QString label = str(preset, "label").toLower();
            const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
            const bool guest = id.startsWith(QStringLiteral("guest:"));
            const QString source = str(preset, "key_source");
            const bool hasKey = preset.value(QStringLiteral("has_stored_key")).toBool();
            const QString limits = relay::models::limitsText(catalog.limits.value(id), now);
            QString status;
            if (id == QStringLiteral("relay-pro")) {
                status = preset.value(QStringLiteral("access_note")).toString(QStringLiteral("enter your personal access code"));
                if (source == QStringLiteral("env")) status += QStringLiteral(" · code from RELAY_RELAY_PRO_API_KEY");
            } else if (hosted) {
                status = preset.value(QStringLiteral("available")).toBool()
                    ? QStringLiteral("included, no key needed") : QStringLiteral("needs python3-cryptography");
            } else if (guest) {
                const QJsonValue loggedIn = preset.value(QStringLiteral("logged_in"));
                status = loggedIn.isBool() ? (loggedIn.toBool() ? QStringLiteral("logged in on this machine")
                                                                : QStringLiteral("not logged in: change login runs the CLI's own sign-in"))
                                           : QStringLiteral("on this machine, runs with your own login");
            } else if (source == QStringLiteral("env")) {
                status = QStringLiteral("key from RELAY_%1_API_KEY").arg(id.toUpper().replace(QLatin1Char('-'), QLatin1Char('_')));
            } else if (hasKey) {
                status = QStringLiteral("key stored in the keyring");
            } else {
                status = QStringLiteral("no key yet · get one at %1").arg(str(preset, "key_url"));
            }
            if (!limits.isEmpty()) status += QStringLiteral(" · ") + limits;
            if (!str(preset, "note").isEmpty()) status += QStringLiteral(" · ") + str(preset, "note").toLower();
            relay::SettingRow row;
            row.kind = relay::SettingRow::Buttons;
            row.id = QStringLiteral("provider:") + id;
            row.label = label;
            row.detail = status;
            row.aliases = QStringLiteral("provider key api keyring login ") + id + QLatin1Char(' ') + str(preset, "provider").toLower();
            row.infoUrl = guest ? (id == QStringLiteral("guest:claude") ? QStringLiteral("https://docs.claude.com/en/docs/claude-code")
                                                                         : QStringLiteral("https://developers.openai.com/codex"))
                        : preset.value(QStringLiteral("custom")).toBool() ? str(preset, "base_url")
                                                                          : str(preset, "key_url");
            row.ruleAbove = !firstProvider;
            firstProvider = false;
            row.dragGroup = QStringLiteral("providers");
            row.onDropBefore = [id, curated](const QString &draggedRowId) {
                relay::models::curation::moveProviderBefore(draggedRowId.section(QLatin1Char(':'), 1), id);
                curated();
            };
            if (id == QStringLiteral("relay-pro")) {
                row.aliases += QStringLiteral(" pro access code password");
                row.buttonTexts = QStringList{hasKey ? QStringLiteral("replace code…") : QStringLiteral("add code…"),
                                              QStringLiteral("check access")};
                if (source == QStringLiteral("keyring")) row.buttonTexts << QStringLiteral("remove");
                row.onButton = [this, request, id](int index) {
                    if (index == 0) {
                        bool ok = false;
                        const QString code = QInputDialog::getText(this, QStringLiteral("Relay Pro"),
                            QStringLiteral("Your personal access code.\nRelay checks it before saving it to the desktop keyring.\nIt is sent only to Relay's hosted service."),
                            QLineEdit::Password, QString(), &ok).trimmed();
                        if (ok && !code.isEmpty()) request({{"type", "store_key"}, {"preset", id}, {"api_key", code}});
                    } else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Remove Pro code"),
                                 QStringLiteral("Remove your Pro access code from this machine's keyring?")) == QMessageBox::Yes)
                        request({{"type", "remove_key"}, {"preset", id}});
                };
            } else if (hosted) {
                if (!preset.value(QStringLiteral("available")).toBool()) { row.kind = relay::SettingRow::Info; row.label = label + QStringLiteral(" · ") + status; }
                else {
                    row.buttonTexts = QStringList{QStringLiteral("test")};   // one real call
                    row.onButton = [request, id](int) { request({{"type", "test_key"}, {"preset", id}}); };
                }
            } else if (guest) {
                // The same two buttons as a keyed provider, in the guest's words: its login is its
                // key. The CLI's own sign-in runs in the pane's terminal; test runs one turn.
                const QString cli = id.mid(6);
                const QString login = cli == QStringLiteral("claude") ? QStringLiteral("claude auth login") : cli + QStringLiteral(" login");
                row.buttonTexts = QStringList{QStringLiteral("change login"), QStringLiteral("test")};
                row.onButton = [this, request, id, login](int index) {
                    if (index == 0) {
                        // The CLI's sign-in needs a terminal to run in: a pane, which the helper is not.
                        if (m_active) m_active->runLoginCommand(login);
                        else QMessageBox::information(this, QStringLiteral("Models"), QStringLiteral("Open a terminal pane first: `%1` runs there.").arg(login));
                    } else request({{"type", "test_key"}, {"preset", id}});
                };
            } else if (preset.value(QStringLiteral("custom")).toBool()) {
                // A custom endpoint (§28.6): edit reopens the form, delete removes it and its key.
                row.buttonTexts = QStringList{QStringLiteral("edit…"), QStringLiteral("test"), QStringLiteral("delete")};
                row.onButton = [this, request, id, label, preset, askForCustom](int index) {
                    if (index == 0) askForCustom(preset);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Delete provider"),
                                 QStringLiteral("Delete %1 and its stored key?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "custom_provider_delete"}, {"provider_id", id}});
                };
            } else {
                row.buttonTexts = QStringList{hasKey ? QStringLiteral("replace key…") : QStringLiteral("add key…"), QStringLiteral("test")};
                if (source == QStringLiteral("keyring")) row.buttonTexts << QStringLiteral("remove");
                row.onButton = [this, request, id, label, askForKey](int index) {
                    if (index == 0) askForKey(id, label);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Remove key"),
                                 QStringLiteral("Remove the stored key for %1 from the keyring?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "remove_key"}, {"preset", id}});
                };
            }
            models.rows << row;
            // ----- step 2, one click from step 1 (card #MDL1, design 5.7) ----------------------
            // "there need to be 4 steps of model availability: 1 add provider, 2 add model as
            // available, 3 add model to priority list, 4 include model in box picker." Step 1 is
            // this row; step 2 is the models pane's **available** tab, and nothing on this page
            // said so. The link opens that tab with this provider's name already typed, and its
            // own count is the answer to "how many of this provider's models am I offering".
            {
                const QList<relay::models::Entry> ofPreset = catalog.ofPreset(id);
                int usable = 0, available = 0;
                for (const relay::models::Entry &entry : ofPreset) {
                    if (!entry.usable) continue;
                    ++usable;
                    if (relay::models::curation::isAvailable(entry, availableKeys)) ++available;
                }
                if (usable > 0) {
                    const QString provider = str(preset, "provider").toLower();
                    relay::SettingRow link = buttonRow(QStringLiteral("models.available:") + id,
                        QStringLiteral("models"),
                        QStringLiteral("Which of this provider's models your lists, the alt+m box and its filter may "
                                       "offer. Opens the models pane's available tab on this provider"),
                        QStringLiteral("models… (%1 of %2 available)").arg(available).arg(usable),
                        [this, provider, label, target = QPointer<Pane>(pane)] {
                            // Follow the provider page's target even if focus moved meanwhile.
                            Pane *on = target ? target.data() : focusedConsole();
                            if (on) on->openModelPicker(QStringLiteral("all"), provider.isEmpty() ? label : provider);
                            else notice(QStringLiteral("Focus a pane first: the models pane always serves one."));
                        });
                    link.aliases = QStringLiteral("available models uncheck enable disable which models step 2 ") + id;
                    link.indent = 1;
                    link.tooltip = link.detail;
                    link.detail.clear();
                    models.rows << link;
                }
            }
            if (guest) {
                // What the guest does when it wants to run a command or change a file. Relay's own
                // agent has no per-action approvals and neither does a guest by default (the
                // owner's rule, 29.1) — but a pane watching a guest work in somebody else's checkout
                // is a fair reason to want the question, so it is offered rather than assumed. It
                // sat under the guest's models until the checklist left the page (t:a10); it is a
                // statement about the provider, so it belongs under the provider's own row.
                const QString cli = id.mid(6);
                const QString key = guestSettingKey(cli, QStringLiteral("permissions"));
                const QString current = QSettings().value(key).toString().trimmed();
                relay::SettingRow ask = choiceRow(QStringLiteral("option:") + key,
                    QStringLiteral("when it wants to use a tool"),
                    QStringLiteral("%1 runs with no per-action approvals, like Relay's own agent. "
                                   "Ask me puts each one to you: Allow, Allow for session, "
                                   "Deny, or Deny and stop the turn").arg(str(preset, "provider").toLower()),
                    {QStringLiteral("bypass"), QStringLiteral("ask"), QStringLiteral("deny")},
                    {QStringLiteral("just run it"), QStringLiteral("ask me"), QStringLiteral("refuse it")},
                    current.isEmpty() ? QStringLiteral("bypass") : current, QStringLiteral("bypass"),
                    [this, key, cli](const QString &value) {
                        if (value.isEmpty() || value == QStringLiteral("bypass")) QSettings().remove(key);
                        else QSettings().setValue(key, value);
                        for (Pane *each : allPanes()) each->guestOptionsChanged(cli);
                        refreshSettingsPanes();
                    });
                ask.aliases = QStringLiteral("claude codex guest permissions approval ask bypass yolo tools sandbox");
                ask.indent = 1;
                // One line like the provider row above it: the explanation is the hover.
                ask.tooltip = ask.detail;
                ask.detail.clear();
                models.rows << ask;
            }
        }
        {
            // The providers without a key, one pick away — and "custom endpoint…" first, always
            // (owner, 2026-09-20): a name, a base URL, a key and model ids, like Warp's custom
            // providers. A built-in pick goes straight to the key box.
            QStringList names;
            for (const QJsonObject &preset : std::as_const(waiting))
                names << str(preset, "provider").section(QStringLiteral(" ("), 0, 0).toLower();
            names.removeDuplicates();
            models.rows << buttonRow(QStringLiteral("models.addProvider"), QStringLiteral("+ add provider"),
                names.isEmpty() ? QStringLiteral("a custom endpoint") : QStringLiteral("a custom endpoint, or %1").arg(names.join(QStringLiteral(", "))),
                QStringLiteral("add…"), [this, waiting, askForKey, askForCustom, str] {
                    QList<relay::agentui::PickerRow> rows;
                    rows << relay::agentui::PickerRow{{QStringLiteral("custom endpoint…"), QStringLiteral("any OpenAI-compatible url"), QString()},
                                                      QStringLiteral("A name, a base URL, a key and model ids"), QStringLiteral("custom")};
                    const auto providerName = [str](const QJsonObject &preset) {
                        return str(preset, "provider").section(QStringLiteral(" ("), 0, 0).toLower();
                    };
                    for (const QJsonObject &preset : waiting)
                        rows << relay::agentui::PickerRow{{providerName(preset), str(preset, "plan").toLower(), str(preset, "key_url")},
                                                          QString(), str(preset, "id")};
                    const auto result = relay::agentui::pick(this, QStringLiteral("add provider"),
                        QStringLiteral("Pick a provider; the next step asks for its key, or for the endpoint."),
                        {QStringLiteral("provider"), QStringLiteral("plan"), QStringLiteral("key page")}, rows,
                        {{QStringLiteral("add"), QStringLiteral("add…"), true}});
                    if (result.row < 0) return;
                    if (result.row == 0) { askForCustom(QJsonObject()); return; }
                    const QJsonObject preset = waiting.at(result.row - 1);
                    askForKey(str(preset, "id"), providerName(preset));
                });
        }
        // One import door below the provider groups. Only the selected source is read, and the
        // worker sends back provider names and counts, never credential values.
        {
            relay::SettingRow import = buttonRow(QStringLiteral("models.importKeys"),
                QStringLiteral("import keys"),
                QStringLiteral("Copy API keys from Warp, OpenCode, Claude Code or Codex into Relay's keyring. "
                               "Subscription sign-ins are not API keys."),
                QStringLiteral("import keys…"), [this, request] {
                    const QStringList sources{QStringLiteral("Warp"), QStringLiteral("OpenCode"),
                        QStringLiteral("Claude Code / Codex API keys")};
                    bool accepted = false;
                    const QString source = QInputDialog::getItem(this, QStringLiteral("Import keys"),
                        QStringLiteral("Import API keys from"), sources, 0, false, &accepted);
                    if (!accepted) return;
                    const QString kind = source == sources.at(0) ? QStringLiteral("import_warp")
                        : source == sources.at(1) ? QStringLiteral("import_opencode")
                        : QStringLiteral("import_agent_tools");
                    request({{QStringLiteral("type"), kind}, {QStringLiteral("id"), QStringLiteral("models-import")}});
                });
            import.aliases = QStringLiteral("import warp opencode claude codex keys keyring migrate");
            models.rows << import;
        }

        // ----- 2. profiles ----------------------------------------------------------------------
        // Owner (2026-09-20 evening): "we need model user profiles like warp for the priority lists
        // … so I can have an 'AI work' profile and an 'admin work' profile that sets different model
        // priorities." Warp's Agent Profile is a whole posture — base model, planning model,
        // autonomy, command allow/deny lists, MCP access — switched from an icon in its input area.
        // Here a profile is the five lists and nothing else, which is what was asked for, and
        // the lists and the profile are one thing: an edit to a list while a profile is current is
        // an edit *of* it, so there is no "unsaved changes" state to explain or lose.
        {
            relay::SettingRow head = headingRow(QStringLiteral("profiles"));
            head.collapsible = true;
            models.rows << head;
        }
        {
            const QStringList names = relay::models::curation::profiles();
            const QString currentProfile = relay::models::curation::currentProfile();
            // Saving the lists as they are under a name, which is also how the first one is made.
            auto saveAs = [this, curated](const QString &initial, const QString &title) {
                bool ok = false;
                const QString name = QInputDialog::getText(this, title,
                    QStringLiteral("A name for the five lists as they are now — “AI work”, “admin work”.\n"
                                   "Editing a list while this profile is chosen edits the profile: there is nothing to save."),
                    QLineEdit::Normal, initial, &ok).trimmed();
                if (!ok || name == initial) return QString();
                if (!relay::models::curation::validProfileName(name)) {
                    if (!name.isEmpty())
                        QMessageBox::warning(this, title, QStringLiteral("A profile name cannot contain “/” or “\\”."));
                    return QString();
                }
                if (relay::models::curation::profiles().contains(name)) {
                    QMessageBox::warning(this, title, QStringLiteral("There is already a profile called “%1”.").arg(name));
                    return QString();
                }
                return name;
            };
            // ----- a profile on disk (owner, 2026-09-21: "allow exporting and importing profiles")
            // JSON, so a profile can be mailed, committed to a dotfiles repo or carried to another
            // machine. The file is the export of one profile or of all of them; the reader takes
            // either, so "export all" and "export this one" import the same way.
            const QString kProfileFilter = QStringLiteral("Relay model profiles (*.json);;All files (*)");
            auto profileDir = [] {
                const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
                return docs.isEmpty() ? QDir::homePath() : docs;
            };
            auto exportProfiles = [this, kProfileFilter, profileDir](const QStringList &names, const QString &suggestion) {
                const QJsonObject document = relay::models::curation::exportProfiles(names);
                const QString title = QStringLiteral("export model profiles");
                if (document.isEmpty()) {
                    QMessageBox::warning(this, title, QStringLiteral("There is nothing to export yet."));
                    return;
                }
                // A profile name is free text; a file name is not. Anything awkward becomes "-".
                QString stem = suggestion;
                stem.replace(QRegularExpression(QStringLiteral("[^\\w .()-]"), QRegularExpression::UseUnicodePropertiesOption),
                             QStringLiteral("-"));
                QString path = QFileDialog::getSaveFileName(this, title,
                                                            QDir(profileDir()).filePath(stem + QStringLiteral(".json")),
                                                            kProfileFilter);
                if (path.isEmpty()) return;
                if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not write %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
                file.close();
                if (file.error() != QFile::NoError) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not write %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                statusBar()->showMessage(names.size() == 1
                                             ? QStringLiteral("Exported “%1” to %2").arg(names.first(), path)
                                             : QStringLiteral("Exported %1 profiles to %2").arg(names.size()).arg(path), 6000);
            };
            auto importProfiles = [this, catalog, curated, kProfileFilter, profileDir] {
                const QString title = QStringLiteral("import model profiles");
                const QString path = QFileDialog::getOpenFileName(this, title, profileDir(), kProfileFilter);
                if (path.isEmpty()) return;
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not read %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                QJsonParseError parse{};
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
                QString error = parse.error == QJsonParseError::NoError ? QString() : parse.errorString();
                const QList<relay::models::curation::ProfileDoc> incoming =
                    error.isEmpty() ? relay::models::curation::readProfiles(document.object(), &error)
                                    : QList<relay::models::curation::ProfileDoc>();
                if (incoming.isEmpty()) {
                    QMessageBox::warning(this, title, QStringLiteral("%1\n\n%2").arg(path, error));
                    return;
                }
                // A name this machine already uses is the one thing that needs an answer: replacing
                // somebody's "AI work" silently is exactly the accident an import should not have.
                auto freeName = [](const QString &base) {
                    const QStringList taken = relay::models::curation::profiles();
                    if (!taken.contains(base)) return base;
                    for (int n = 2; n < 1000; ++n) {
                        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
                        if (!taken.contains(candidate)) return candidate;
                    }
                    return base;
                };
                const QString wasCurrent = relay::models::curation::currentProfile();
                QStringList added, skipped;
                for (relay::models::curation::ProfileDoc profile : incoming) {
                    if (relay::models::curation::profiles().contains(profile.name)) {
                        QMessageBox box(QMessageBox::Question, title,
                                        QStringLiteral("This machine already has a profile called “%1”.")
                                            .arg(profile.name),
                                        QMessageBox::NoButton, this);
                        box.setInformativeText(profile.name == wasCurrent
                                                   ? QStringLiteral("Replacing it also moves the five lists onto the imported "
                                                                    "ones — they are the lists this profile names.")
                                                   : QStringLiteral("Replacing it overwrites its five lists."));
                        // All three are ActionRole bar the last, so they keep this order in every
                        // button layout: the destructive one first, the safe one default.
                        box.addButton(QStringLiteral("replace"), QMessageBox::ActionRole);
                        QPushButton *both = box.addButton(QStringLiteral("keep both"), QMessageBox::ActionRole);
                        QPushButton *skip = box.addButton(QStringLiteral("skip"), QMessageBox::RejectRole);
                        box.setDefaultButton(both);
                        box.exec();
                        if (box.clickedButton() == skip) { skipped << profile.name; continue; }
                        if (box.clickedButton() == both) profile.name = freeName(profile.name);
                    }
                    relay::models::curation::writeProfile(profile);
                    added << profile.name;
                }
                if (added.isEmpty()) return;
                // An import that replaced the *current* profile moved the live lists with it, and
                // `curated()` carries that to every pane and its worker either way (card #MDL1:
                // there is no second copy of the default left to write when it does).
                curated();
                QMessageBox::information(this, title,
                                         QStringLiteral("Imported %1.%2\nChoose one in the profile box to switch the five "
                                                        "lists onto it.")
                                             .arg(QStringLiteral("“") + added.join(QStringLiteral("”, “")) + QStringLiteral("”"),
                                                  skipped.isEmpty() ? QString()
                                                                    : QStringLiteral(" Skipped %1.").arg(skipped.size())));
            };
            const QString kNew = QStringLiteral("\x01new");   // never a profile name: validProfileName trims
            relay::SettingRow row;
            row.kind = relay::SettingRow::Choice;
            row.id = QStringLiteral("models/profile");
            row.label = QStringLiteral("profile");
            row.detail = QStringLiteral("A named set of the five lists. Switching one in swaps every list at once "
                                        "(also /profile)");
            row.aliases = QStringLiteral("profile profiles preset workspace ai work admin work priorities switch");
            // "no profile" is offered only while that is where you are: once a profile is chosen the
            // lists belong to it, and the way out is to delete it, as in Warp.
            if (currentProfile.isEmpty()) { row.options << QString(); row.optionLabels << QStringLiteral("no profile"); }
            for (const QString &name : names) { row.options << name; row.optionLabels << name; }
            row.options << kNew;
            row.optionLabels << QStringLiteral("new profile…");
            row.current = currentProfile;
            row.onChoose = [this, catalog, curated, saveAs, kNew](const QString &value) {
                if (value == kNew) {
                    const QString name = saveAs(QString(), QStringLiteral("new profile"));
                    if (name.isEmpty()) { refreshSettingsPanes(); return; }   // put the box back on what is current
                    relay::models::curation::saveProfile(name);
                    curated();
                    return;
                }
                if (value.isEmpty()) return;
                relay::models::curation::applyProfile(value);
                curated();   // rank 1 of the new main list is what the next new pane starts on
            };
            models.rows << row;
            if (!currentProfile.isEmpty()) {
                relay::SettingRow actions;
                actions.kind = relay::SettingRow::Buttons;
                actions.id = QStringLiteral("models.profile.actions");
                actions.label = currentProfile;
                actions.indent = 1;
                actions.tooltip = QStringLiteral("The profile the five lists belong to right now");
                actions.aliases = QStringLiteral("rename delete profile ") + currentProfile;
                actions.buttonTexts = QStringList{QStringLiteral("rename…"), QStringLiteral("export…"), QStringLiteral("delete")};
                actions.onButton = [this, currentProfile, curated, saveAs, exportProfiles](int index) {
                    if (index == 0) {
                        const QString name = saveAs(currentProfile, QStringLiteral("rename profile"));
                        if (name.isEmpty()) return;
                        relay::models::curation::renameProfile(currentProfile, name);
                        curated();
                        return;
                    }
                    if (index == 1) { exportProfiles(QStringList{currentProfile}, currentProfile); return; }
                    if (QMessageBox::question(this, QStringLiteral("delete profile"),
                                              QStringLiteral("Delete the profile “%1”?\nThe five lists stay exactly as they are; "
                                                             "they simply stop belonging to a profile.").arg(currentProfile))
                        != QMessageBox::Yes)
                        return;
                    relay::models::curation::deleteProfile(currentProfile);
                    curated();
                };
                models.rows << actions;
            }
            {
                // Always here, profiles or none: importing is how the first profile arrives on a
                // second machine. "export all" only appears once there is more than one to mean.
                relay::SettingRow file;
                file.kind = relay::SettingRow::Buttons;
                file.id = QStringLiteral("models.profile.file");
                file.label = QStringLiteral("profiles file");
                file.tooltip = QStringLiteral("JSON you can mail, commit to your dotfiles, or carry to another machine. "
                                              "A file holds one profile or all of them; either imports.");
                file.aliases = QStringLiteral("import export profile profiles file json backup share carry");
                file.buttonTexts = QStringList{QStringLiteral("import…")};
                if (names.size() > 1) file.buttonTexts << QStringLiteral("export all…");
                file.onButton = [names, exportProfiles, importProfiles](int index) {
                    if (index == 0) { importProfiles(); return; }
                    exportProfiles(names, QStringLiteral("relay model profiles"));
                };
                models.rows << file;
            }
        }

        // ----- 3. defaults ----------------------------------------------------------------------
        { relay::SettingRow head = headingRow(QStringLiteral("defaults")); head.collapsible = true; models.rows << head; }
        {
            relay::SettingRow failover = toggleRow(QStringLiteral("agent/failover"), QStringLiteral("Fall over to a fallback model"),
                                                  QStringLiteral("A turn whose model keeps failing, after its retries, continues down "
                                                                 "the list it is on — that turn only. The pane keeps the model you chose"), true);
            failover.aliases = QStringLiteral("failover fallback retry provider down error 429 overloaded relay free");
            models.rows << failover;
        }
        models.rows << numberRow(QStringLiteral("provider/max_tokens"), QStringLiteral("Output token limit"),
                                 QStringLiteral("Per model call, reasoning included. 0 = automatic: each model's own "
                                                "documented limit (GLM 131072, Gemini 65536). Applies to the next conversation"),
                                 0, 0, 131072);
        // "Advanced provider settings" was here (card #MDL1, 2026-09-21: "check the advanced
        // provider settings. not sure whats helpful or needed"). Every field of it had grown a
        // better home — the preset combo and the key are the providers rows above, a base URL and
        // a model id are a custom endpoint on "+ add provider", the output token limit is the row
        // above this one, the agent workspace is the pane's own directory, the Warp import is a
        // row of its own at the bottom of the providers, and the consent sentence is in the key
        // box where a key is actually typed. Design 5.8 has the reasoning.
        models.rows << buttonRow(QStringLiteral("agent.modelRoles"), QStringLiteral("per-job models"),
                                 QStringLiteral("What each job — plan mode, subagents, summaries, chores — runs on "
                                                "right now, and a model of its own for one: the models pane's jobs tab"),
                                 QStringLiteral("jobs…"), [this] { runAction(QStringLiteral("agent.modelRoles")); });
        // Keep one source for the controls, but arrange the Models pane's provider page around
        // the account the key pays for. The saved provider order still applies within each group.
        const QList<relay::SettingRow> original = models.rows;
        int providerAt = -1, profilesAt = -1, defaultsAt = -1;
        for (int i = 0; i < original.size(); ++i) {
            const QString id = original.at(i).id;
            if (id == QStringLiteral("heading:providers")) providerAt = i;
            else if (id == QStringLiteral("heading:profiles")) profilesAt = i;
            else if (id == QStringLiteral("heading:defaults")) defaultsAt = i;
        }
        Q_ASSERT(providerAt >= 0 && profilesAt > providerAt && defaultsAt > profilesAt);
        QList<relay::SettingRow> arranged;
        if (!inModelsPane) arranged << original.mid(0, providerAt); // Options' link to the pane
        arranged << original.mid(profilesAt, defaultsAt - profilesAt);
        QList<relay::SettingRow> groups[3];
        QList<relay::SettingRow> beforeGroups;
        QList<relay::SettingRow> afterGroups;
        QList<relay::SettingRow> providerRows;
        QString providerId;
        const auto flushProvider = [&] {
            if (providerRows.isEmpty()) return;
            int group = 2; // API keys, OpenRouter, and custom endpoints
            if (providerId.startsWith(QStringLiteral("guest:"))) group = 0;
            else for (const QJsonValue &value : presets) {
                const QJsonObject preset = value.toObject();
                if (preset.value(QStringLiteral("id")).toString() != providerId) continue;
                if (preset.value(QStringLiteral("kind")).toString() == QStringLiteral("plan")) group = 1;
                break;
            }
            groups[group] << providerRows;
            providerRows.clear();
        };
        for (int i = providerAt + 1; i < profilesAt; ++i) {
            const relay::SettingRow &row = original.at(i);
            if (row.id.startsWith(QStringLiteral("provider:"))) {
                flushProvider();
                providerId = row.id.mid(QStringLiteral("provider:").size());
                providerRows << row;
            } else if (row.id == QStringLiteral("models.importKeys")) {
                flushProvider();
                afterGroups << row;
            } else if (row.id == QStringLiteral("models.addProvider") || row.id == QStringLiteral("info:models/none")) {
                flushProvider();
                beforeGroups << row;
            } else {
                providerRows << row; // models link or guest permissions under its provider
            }
        }
        flushProvider();
        arranged << beforeGroups;
        const QStringList labels{QStringLiteral("Guest Agents"), QStringLiteral("Subscription Keys"),
                                 QStringLiteral("Pay-as-you-go Keys")};
        for (int i = 0; i < 3; ++i) {
            relay::SettingRow head = headingRow(labels.at(i));
            head.collapsible = true;
            arranged << head;
            bool first = true;
            for (relay::SettingRow row : groups[i]) {
                if (row.id.startsWith(QStringLiteral("provider:"))) {
                    row.dragGroup = QStringLiteral("providers.") + QString::number(i);
                    if (first) row.ruleAbove = false;
                    first = false;
                }
                arranged << row;
            }
        }
        arranged << afterGroups;
        if (!inModelsPane) {
            arranged << original.mid(defaultsAt);
            relay::SettingRow fill;
            fill.kind = relay::SettingRow::Buttons;
            fill.id = QStringLiteral("models.tier.defaults");
            fill.label = QStringLiteral("Model priority lists");
            fill.detail = QStringLiteral("Replace all priority lists with the models your providers offer.");
            fill.buttonTexts = QStringList{QStringLiteral("Fill from defaults"), QStringLiteral("…with OpenRouter")};
            fill.onButton = [this, target = QPointer<Pane>(pane)](int index) {
                if (!target || !target->fillTierListsFromDefaults(index == 1)) {
                    QMessageBox::information(this, QStringLiteral("Model defaults"),
                        QStringLiteral("No defaults yet — open a pane's agent first, so its worker can compute them."));
                    return;
                }
                modelsCurated();
            };
            arranged << fill;
        }
        models.rows = arranged;
        return models;
    }
