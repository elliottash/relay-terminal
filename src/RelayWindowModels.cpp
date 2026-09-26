// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"
#include "WindowManagerImpl.h"

#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

// ----- the usage chart (#62TG) ------------------------------------------------------------------
// The models pane's usage row opens this: one row per account in the main list's draw, every window
// the account reported (5h and weekly) as a bar with what is left and how long until it resets,
// then the weight the router gives the account and the share it wins. The weights and shares come
// from relay::models::usageChartRows — the same function the draw uses — so the chart cannot drift
// from the routing it explains, and an exhausted account stays as a red zero row instead of
// vanishing from the picture.
namespace {

// What one account's block needs: its name, a line and a bar per window, its weight-and-share line.
int usageBlockHeight(const relay::models::UsageChartRow &row) {
    return 20 + qMax(1, int(row.windows.size())) * 34 + 24;
}

QString usageWindowLine(const relay::models::LimitWindow &window, qint64 now) {
    const double left = window.usedPercent < 0 ? -1.0 : qBound(0.0, 100.0 - window.usedPercent, 100.0);
    const QString share = left < 0 ? QStringLiteral("no figure")
                                   : QStringLiteral("%1% left").arg(QString::number(left, 'f', 0));
    QString reset = QStringLiteral("reset unknown");
    if (window.resetsAt > 0) {
        const double hours = qMax(0.0, double(window.resetsAt - now) / 3600.0);
        reset = QStringLiteral("resets in %1 h · %2")
                    .arg(QString::number(hours, 'f', hours < 10 ? 1 : 0),
                         QDateTime::fromSecsSinceEpoch(window.resetsAt).toString(QStringLiteral("ddd HH:mm")));
    }
    return QStringLiteral("%1   %2 · %3").arg(window.kind.isEmpty() ? QStringLiteral("window") : window.kind, share, reset);
}

// The chart itself. It is painted rather than laid out: bars want exact heights, and a row per
// account does not need a widget row per account. No Q_OBJECT — nothing here is connected.
class UsageChartView : public QWidget {
public:
    explicit UsageChartView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumWidth(780);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }
    void setRows(const QList<relay::models::UsageChartRow> &rows, const QString &tier, qint64 now) {
        m_rows = rows;
        m_tier = tier;
        m_now = now;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override {
        int height = 14;
        for (const relay::models::UsageChartRow &row : m_rows) height += usageBlockHeight(row) + 8;
        return QSize(940, qMax(120, height));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();
        const QColor textColour = pal.color(QPalette::WindowText);
        const QColor dimColour = pal.color(QPalette::Disabled, QPalette::WindowText);
        const QColor rule = pal.color(QPalette::Mid);
        QFont bold = font();
        bold.setBold(true);
        QFont small = font();
        small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));

        const int left = 18;
        const int nameWidth = 232;
        const int rightWidth = 150;
        const int textLeft = left + nameWidth + 10;
        const int barLeft = textLeft;
        const int barWidth = qMax(120, width() - rightWidth - 14 - barLeft);
        const int textWidth = qMax(120, barWidth);

        int y = 8;
        for (const relay::models::UsageChartRow &row : m_rows) {
            const QColor accent = row.exhausted ? QColor(0xb7, 0x1c, 0x1c) : textColour;
            painter.setFont(bold);
            painter.setPen(accent);
            painter.drawText(QRect(left, y, nameWidth, 18), Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(row.label, Qt::ElideMiddle, nameWidth));
            painter.setFont(small);
            painter.setPen(dimColour);
            painter.drawText(QRect(textLeft, y, textWidth, 18), Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(row.model, Qt::ElideRight, textWidth));

            const QList<relay::models::LimitWindow> windows =
                row.windows.isEmpty() ? QList<relay::models::LimitWindow>{relay::models::LimitWindow{}} : row.windows;
            int rowY = y + 20;
            for (const relay::models::LimitWindow &window : windows) {
                const double leftPercent = window.usedPercent < 0 ? 0.0 : qBound(0.0, 100.0 - window.usedPercent, 100.0);
                painter.setFont(small);
                painter.setPen(leftPercent <= 0 ? QColor(0xb7, 0x1c, 0x1c) : dimColour);
                painter.drawText(QRect(barLeft, rowY, textWidth, 15), Qt::AlignLeft | Qt::AlignVCenter,
                                 painter.fontMetrics().elidedText(usageWindowLine(window, m_now), Qt::ElideRight, textWidth));
                const QRect track(barLeft, rowY + 17, barWidth, 9);
                painter.setPen(Qt::NoPen);
                QColor fill = rule;
                fill.setAlpha(90);
                painter.setBrush(fill);
                painter.drawRoundedRect(track, 4, 4);
                if (leftPercent > 0) {
                    const QColor bar = leftPercent < 20 ? QColor(0xef, 0x6c, 0x00) : QColor(0x2e, 0x7d, 0x32);
                    painter.setBrush(bar);
                    painter.drawRoundedRect(QRect(track.x(), track.y(),
                                                  int(track.width() * leftPercent / 100.0), track.height()), 4, 4);
                }
                rowY += 34;
            }

            painter.setFont(small);
            const QString figures = row.exhausted
                ? QStringLiteral("exhausted · out of the draw")
                : QStringLiteral("weight %1 · draw %2")
                      .arg(QString::number(row.weight, 'f', 2), QStringLiteral("%1%").arg(QString::number(row.probability * 100.0, 'f', 1)));
            painter.setPen(row.exhausted ? QColor(0xb7, 0x1c, 0x1c) : dimColour);
            painter.drawText(QRect(barLeft, rowY - 4, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, figures);
            if (row.stale && !row.exhausted) {
                painter.setPen(dimColour);
                painter.drawText(QRect(barLeft, rowY - 4, textWidth, 16), Qt::AlignRight | Qt::AlignVCenter,
                                 QStringLiteral("no fresh figures · neutral weight"));
            }

            y += usageBlockHeight(row);
            painter.setPen(rule);
            painter.drawLine(left, y - 4, width() - 14, y - 4);
            y += 8;
        }
        if (m_rows.isEmpty()) {
            painter.setFont(small);
            painter.setPen(dimColour);
            painter.drawText(QRect(left, 12, width() - 2 * left, 40), Qt::AlignLeft | Qt::AlignTop,
                             QStringLiteral("No account is in the %1 list's draw yet: no subscription has reported usage.").arg(m_tier));
        }
    }

private:
    QList<relay::models::UsageChartRow> m_rows;
    QString m_tier;
    qint64 m_now = 0;
};

// The window the models pane opens. One chart, no controls: what it draws is read live when it opens,
// and the pane's usage… button opens it again for a fresh look.
class UsageChartDialog : public QDialog {
public:
    UsageChartDialog(const QList<relay::models::UsageChartRow> &rows, const QString &tier, qint64 now, QWidget *parent)
        : QDialog(parent) {
        setObjectName(QStringLiteral("usageChartDialog"));
        setWindowTitle(QStringLiteral("Subscription usage and draw odds"));
        auto *layout = new QVBoxLayout(this);
        auto *blurb = new QLabel(
            QStringLiteral("Every account the %1 list draws on, with each window it reports — 5h and weekly — "
                           "the share of that window still unspent, and the time until it resets. The weight is the "
                           "tightest window's share left over the hours until it resets; the draw column is that weight "
                           "as a share of every account at rank 1. Red rows are exhausted and get nothing until they reset.")
                .arg(tier),
            this);
        blurb->setWordWrap(true);
        blurb->setObjectName(QStringLiteral("usageChartBlurb"));
        layout->addWidget(blurb);
        m_view = new UsageChartView(this);
        m_view->setObjectName(QStringLiteral("usageChartView"));
        m_view->setRows(rows, tier, now);
        layout->addWidget(m_view);
        resize(960, qBound(360, m_view->sizeHint().height() + 110, 1100));
    }
    UsageChartView *view() const { return m_view; }

private:
    UsageChartView *m_view = nullptr;
};

}   // namespace

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
            none.label = QStringLiteral("Starting the agent to read your providers…");
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
            // ruling (RELAY.md), so a tick that only said "yes, run tools" would gate nothing and
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
        // A provider is a single row — its key (card #WBFM retired the "models… (N of M)" link
        // and the guest tool-permission row) — and the rows ran into the next provider's as one
        // wall of text (owner, 2026-09-21: "tab 1: add horizontal line dividers between providers"). The
        // rule goes on each provider's own row but the first's, in the theme's `@border`, which is
        // the colour a section heading is underlined in.
        // Usage is read every 15 minutes and after each guest turn; this reads every subscription
        // now — Claude Code and Codex logins and accounts, the Z.AI Coding Plan, Kimi Code (#EQH0).
        // The answers arrive as the usual usage_limits events and redraw the rows below.
        // The second button opens the chart (#62TG): the same usage account by account, with the
        // weight and the draw share the router would give each one right now. The pane's own account
        // names are the chart's labels, so an account that reads "logged in as ashe@ethz.ch" on its
        // row reads the same in the chart (#EQH0).
        QHash<QString, QString> accountLabels;
        for (const QJsonObject &preset : std::as_const(listed)) {
            const QString id = str(preset, "id");
            if (id.isEmpty()) continue;
            const QString email = str(preset, "email");
            accountLabels.insert(id, email.isEmpty() ? id : email);
        }
        relay::SettingRow usageRow = buttonRow(QStringLiteral("models.refreshUsage"), QStringLiteral("usage"),
            QStringLiteral("each subscription's 5-hour and weekly use, read every 15 minutes"),
            QStringLiteral("refresh"), [this, request] {
                request({{"type", "usage_refresh"}});
                notice(QStringLiteral("Asking every subscription for its usage…"));
            });
        usageRow.kind = relay::SettingRow::Buttons;
        usageRow.buttonTexts = QStringList{QStringLiteral("refresh"), QStringLiteral("usage…")};
        usageRow.aliases = QStringLiteral("chart odds weights quota 5h weekly draw");
        usageRow.onButton = [this, request, catalog, accountLabels](int index) {
            if (index == 0) {
                request({{"type", "usage_refresh"}});
                notice(QStringLiteral("Asking every subscription for its usage…"));
                return;
            }
            QList<relay::models::UsageChartRow> rows =
                relay::models::usageChartRows(catalog, QStringLiteral("main"));
            for (relay::models::UsageChartRow &row : rows)
                if (accountLabels.contains(row.preset)) row.label = accountLabels.value(row.preset);
            auto *dialog = new UsageChartDialog(rows, QStringLiteral("main"),
                                                QDateTime::currentSecsSinceEpoch(), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        };
        models.rows << usageRow;
        bool firstProvider = true;
        std::function<void(int)> addClaudeAccount;
        std::function<void(int)> addCodexAccount;
        for (const QJsonObject &preset : std::as_const(listed)) {
            const QString id = str(preset, "id");
            const QString label = str(preset, "label").toLower();
            const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
            const bool guest = id.startsWith(QStringLiteral("guest:"));
            const QString source = str(preset, "key_source");
            const bool hasKey = preset.value(QStringLiteral("has_stored_key")).toBool();
            // The row's own figures are the *main* stats — one short pair per window, "5h 75% · wk
            // 54%" — so a page of providers reads at a glance next to the models each one serves.
            // The long form, with every window's reset and everything the weight is made of, is the
            // usage… chart's job (#62TG) and this row's tooltip; on the page it was the part that
            // pushed the account name off the line.
            const QString limits = relay::models::usageStatsText(catalog.limits.value(id), now);
            const QString fullLimits = relay::models::limitsText(catalog.limits.value(id), now,
                                                                 catalog.resetsAvailable.value(id, -1),
                                                                 catalog.resetsExpireAt.value(id));
            QString status;
            if (id == QStringLiteral("relay-pro")) {
                status = preset.value(QStringLiteral("access_note")).toString(QStringLiteral("enter your personal access code"));
                if (source == QStringLiteral("env")) status += QStringLiteral(" · code from RELAY_RELAY_PRO_API_KEY");
            } else if (hosted) {
                status = preset.value(QStringLiteral("available")).toBool()
                    ? QStringLiteral("included, no key needed") : QStringLiteral("needs python3-cryptography");
            } else if (guest) {
                const QJsonValue loggedIn = preset.value(QStringLiteral("logged_in"));
                const bool account = !str(preset, "account").isEmpty();   // #M8S2
                // Which account it is (#EQH0): the address the login's own files name.
                const QString email = str(preset, "email");
                status = loggedIn.isBool() ? (loggedIn.toBool() ? (email.isEmpty() ? QStringLiteral("logged in on this machine")
                                                                                  : QStringLiteral("logged in as ") + email)
                                                                : account ? QStringLiteral("not logged in: sign in runs the CLI's own sign-in for this account")
                                                                          : QStringLiteral("not logged in: change login runs the CLI's own sign-in"))
                                           : account ? QStringLiteral("a separate login of this CLI, in its own directory")
                                                     : QStringLiteral("on this machine, runs with your own login");
                if (!email.isEmpty() && !(loggedIn.isBool() && loggedIn.toBool())) status += QStringLiteral(" · ") + email;
            } else if (source == QStringLiteral("env")) {
                status = QStringLiteral("key from RELAY_%1_API_KEY").arg(id.toUpper().replace(QLatin1Char('-'), QLatin1Char('_')));
            } else if (source == QStringLiteral("local")) {
                status = QStringLiteral("no key needed (local endpoint)");   // #BYG1
            } else if (hasKey) {
                status = QStringLiteral("key stored in the keyring");
            } else {
                status = QStringLiteral("no key yet · get one at %1").arg(str(preset, "key_url"));
            }
            // The agent catalog gets the status without the live figures (#BT7C): they change on
            // every usage event and every minute, and a catalog that differs is re-sent to every
            // worker. Options still draws them.
            const QString note = str(preset, "note").isEmpty() ? QString() : QStringLiteral(" · ") + str(preset, "note").toLower();
            const QString stableStatus = status + note;
            if (!limits.isEmpty()) status += QStringLiteral(" · ") + limits;
            status += note;
            relay::SettingRow row;
            row.kind = relay::SettingRow::Buttons;
            row.id = QStringLiteral("provider:") + id;
            row.label = label;
            if (guest && str(preset, "account").isEmpty() && !str(preset, "email").isEmpty())
                row.label += QStringLiteral(" (") + str(preset, "email") + QLatin1Char(')');
            row.detail = status;
            // The long usage line, resets and all, stays one hover away (#62TG).
            if (!fullLimits.isEmpty()) row.tooltip = fullLimits;
            if (stableStatus != status) row.agentDetail = stableStatus;
            row.aliases = QStringLiteral("provider key api keyring login ") + id + QLatin1Char(' ') + str(preset, "provider").toLower();
            if (!str(preset, "email").isEmpty()) row.aliases += QStringLiteral(" email ") + str(preset, "email");
            row.infoUrl = guest ? (id.startsWith(QStringLiteral("guest:claude")) ? QStringLiteral("https://docs.claude.com/en/docs/claude-code")
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
                // A registered account (#M8S2) is a row of its own: its sign-in carries its config
                // directory, and "remove" takes the name out of Relay without signing anybody out.
                const QString cli = id.mid(6).section(QLatin1Char(':'), 0, 0);
                const QString account = str(preset, "account");
                const QString login = !str(preset, "login_command").isEmpty() ? str(preset, "login_command")
                    : cli == QStringLiteral("claude") ? QStringLiteral("claude auth login") : cli + QStringLiteral(" login");
                row.buttonTexts = account.isEmpty()
                    ? QStringList{QStringLiteral("change login"), QStringLiteral("test")}
                    : QStringList{QStringLiteral("sign in"), QStringLiteral("test"), QStringLiteral("remove")};
                if (!account.isEmpty()) {
                    row.aliases += QStringLiteral(" account subscription ") + account + QLatin1Char(' ') + str(preset, "config_dir");
                    row.tooltip = QStringLiteral("Runs with %1=%2")
                        .arg(cli == QStringLiteral("claude") ? QStringLiteral("CLAUDE_CONFIG_DIR") : QStringLiteral("CODEX_HOME"),
                             str(preset, "config_dir"));
                }
                const QString guestLabel = str(preset, "provider").isEmpty() ? label : str(preset, "provider");
                row.onButton = [this, request, id, login, cli, account, label, guestLabel](int index) {
                    if (index == 0) {
                        // The CLI's sign-in needs a terminal to run in: a pane, which the helper is not.
                        if (m_active) m_active->runLoginCommand(login);
                        else QMessageBox::information(this, QStringLiteral("Models"), QStringLiteral("Open a terminal pane first: `%1` runs there.").arg(login));
                    } else if (index == 1) {
                        request({{"type", "test_key"}, {"preset", id}});
                    } else if (account.isEmpty()) {
                        // Another login of the same CLI, side by side with this one: a name and a
                        // directory of its own. Empty directory is a new one under Relay's config,
                        // signed in next; an existing one (already signed in) is used as it is.
                        QDialog dialog(this);
                        dialog.setWindowTitle(QStringLiteral("Add %1 account").arg(guestLabel.toLower()));
                        auto *form = new QFormLayout(&dialog);
                        auto *about = new QLabel(QStringLiteral("Another %1 login beside this one, with its own subscription, "
                                                                "usage limits and sessions.").arg(guestLabel.toLower()), &dialog);
                        about->setWordWrap(true);
                        form->addRow(about);
                        auto *name = new QLineEdit(&dialog);
                        name->setPlaceholderText(QStringLiteral("work, personal, eth…"));
                        auto *dir = new QLineEdit(&dialog);
                        dir->setPlaceholderText(QStringLiteral("empty: a new directory, signed in next"));
                        form->addRow(QStringLiteral("name"), name);
                        form->addRow(QStringLiteral("config directory"), dir);
                        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                        auto *withHelper = buttons->addButton(QStringLiteral("Set up with agent"),
                                                               QDialogButtonBox::ActionRole);
                        connect(withHelper, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
                        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, name] {
                            if (!name->text().trimmed().isEmpty()) dialog.accept();
                        });
                        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                        form->addRow(buttons);
                        dialog.resize(520, dialog.sizeHint().height());
                        const int choice = dialog.exec();
                        if (choice == 2) {
                            // Open the one Models helper after this modal's row callback unwinds.
                            // The /skill prefix attaches the bundled interview to the first turn;
                            // no account is saved until the person chooses to use the manual form.
                            QString prompt = QStringLiteral("/skill guest-account-setup Help me set up another %1 "
                                "subscription in Relay. Inspect my current Providers setup, ask me one question "
                                "at a time, then tell me what to do. Do not create an account or sign in yet.")
                                .arg(cli == QStringLiteral("claude") ? QStringLiteral("Claude Code") : QStringLiteral("Codex"));
                            if (!name->text().trimmed().isEmpty())
                                prompt += QStringLiteral(" I had entered the account name: %1.").arg(name->text().trimmed());
                            if (!dir->text().trimmed().isEmpty())
                                prompt += QStringLiteral(" I had entered this config directory: %1.").arg(dir->text().trimmed());
                            QTimer::singleShot(0, this, [this, prompt] {
                                Pane *served = m_active ? m_active.data() : focusedConsole();
                                openModelsPaneFor(served, served ? served->modelsTarget() : relay::ModelsPane::Target(),
                                                  relay::ModelsPane::providersTab(), QString());
                                auto *view = modelsViewOf(modelsPaneIn(m_tabs->currentWidget()));
                                if (!view) return;
                                view->focusHelper();
                                if (auto *console = dynamic_cast<Pane *>(view->agentConsole().widget))
                                    QTimer::singleShot(800, console, [this, console, prompt] {
                                        const QString preset = console->currentPreset();
                                        if (console->agentReady() && !preset.isEmpty()
                                            && !preset.startsWith(QStringLiteral("guest:"))) console->askAgent(prompt);
                                        else {
                                            console->draftInComposer(prompt);
                                            notice(QStringLiteral("Choose a non-guest model for the agent, then send the prepared setup request."));
                                        }
                                    });
                            });
                            return;
                        }
                        if (choice != QDialog::Accepted) return;
                        QJsonObject spec{{QStringLiteral("guest"), cli}, {QStringLiteral("label"), name->text().trimmed()}};
                        const QString path = QDir::fromNativeSeparators(dir->text().trimmed());
                        if (!path.isEmpty()) spec.insert(QStringLiteral("config_dir"), path);
                        request({{"type", "guest_account_save"}, {"id", QStringLiteral("guest-account")},
                                 {"account", spec}, {"sign_in", path.isEmpty()}});
                    } else if (QMessageBox::question(this, QStringLiteral("Remove account"),
                                   QStringLiteral("Remove %1 from Relay? Its login directory is kept, and adding it back "
                                                  "with the same directory needs no new sign-in.").arg(label)) == QMessageBox::Yes) {
                        request({{"type", "guest_account_delete"}, {"id", QStringLiteral("guest-account")},
                                 {"key", cli + QLatin1Char(':') + account}});
                    }
                };
                if (account.isEmpty()) {
                    if (cli == QStringLiteral("claude")) addClaudeAccount = row.onButton;
                    else if (cli == QStringLiteral("codex")) addCodexAccount = row.onButton;
                }
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
            } else if (!str(preset, "base_preset").isEmpty()) {
                // A second subscription of a plan (#YC0T): its own key, and "remove" forgets the
                // account and its key together — there is no account without one.
                row.aliases += QStringLiteral(" account subscription ") + str(preset, "account_label");
                row.buttonTexts = QStringList{QStringLiteral("replace key…"), QStringLiteral("test"), QStringLiteral("remove")};
                row.onButton = [this, request, id, label, askForKey](int index) {
                    if (index == 0) askForKey(id, label);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Remove account"),
                                 QStringLiteral("Remove %1 and its key from Relay?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "key_account_delete"}, {"key", id}});
                };
            } else {
                row.buttonTexts = QStringList{hasKey ? QStringLiteral("replace key…") : QStringLiteral("add key…"), QStringLiteral("test")};
                if (source == QStringLiteral("keyring")) row.buttonTexts << QStringLiteral("remove");
                // Another subscription of the same plan, side by side (#YC0T): a name and its key.
                const bool accounts = preset.value(QStringLiteral("accounts_allowed")).toBool();
                if (accounts) row.buttonTexts << QStringLiteral("add account…");
                const int addAt = row.buttonTexts.size() - 1;
                row.onButton = [this, request, id, label, askForKey, accounts, addAt](int index) {
                    if (index == 0) askForKey(id, label);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (accounts && index == addAt) {
                        bool ok = false;
                        const QString name = QInputDialog::getText(this, QStringLiteral("Add %1 account").arg(label),
                            QStringLiteral("A name for the other subscription (e.g. work, ethz).\n"
                                           "It gets its own key, usage and place in the pick order."),
                            QLineEdit::Normal, QString(), &ok).trimmed();
                        if (!ok || name.isEmpty()) return;
                        const QString key = QInputDialog::getText(this, QStringLiteral("Add %1 account").arg(label),
                            QStringLiteral("The API key of the %1 subscription.\nIt is saved to the desktop keyring.").arg(name),
                            QLineEdit::Password, QString(), &ok).trimmed();
                        if (!ok || key.isEmpty()) return;
                        request({{"type", "key_account_save"},
                                 {"account", QJsonObject{{"preset", id}, {"label", name}, {"api_key", key}}}});
                    } else if (QMessageBox::question(this, QStringLiteral("Remove key"),
                                 QStringLiteral("Remove the stored key for %1 from the keyring?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "remove_key"}, {"preset", id}});
                };
            }
            models.rows << row;
            if (guest && str(preset, "account").isEmpty())
                // The permissions choice row is gone (card #WBFM; rule 29.1 says a guest always
                // just runs it): drop any value a user stored while the row was offered.
                QSettings().remove(guestSettingKey(id.mid(6), QStringLiteral("permissions")));
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
        // ----- 3b. per-job rules (#WK7C) ----------------------------------------------------------
        // An agent with permission sets a job's model the way it sets any other option: one row
        // per settable job, ids `roles.<role>` (protocol §30), writing the same rolestore keys
        // the job rules table writes — one source of truth, so a job's rule no longer needs the
        // table to change it. The value encodes the rule: empty follows the job's tier,
        // `tier:<tier>` is a tier follow, a `preset|model` key is a pin; "ranked" reads what a
        // ranked list is while staying the table's own to edit. Collapsed by default — Options ›
        // Models is not the rich view, and the table row below stays the door to it.
        {
            relay::SettingRow head = headingRow(QStringLiteral("per-job models"));
            head.id = QStringLiteral("heading:roles");
            head.collapsible = true;
            head.collapsedByDefault = true;
            head.tooltip = QStringLiteral("Each job's model rule, the way an agent sets it. The rules apply to "
                                          "every pane; the models pane's job rules tab shows what each pane's "
                                          "worker last resolved.");
            models.rows << head;
        }
        for (const relay::JobsTab::Job &job : relay::JobsTab::jobs()) {
            // Agent turns are not a rule — each pane runs on its own active model — and the table
            // says so; a text cell here would read as a setting that never applies.
            if (!job.settable) continue;
            const QString follows = job.tier == QStringLiteral("fixed")
                ? QStringLiteral("automatic — relay picks it")
                : QStringLiteral("follows %1").arg(job.tier);
            relay::SettingRow row;
            row.id = QStringLiteral("roles.%1").arg(job.role);
            row.label = job.name;
            row.kind = relay::SettingRow::Kind::Text;
            row.detail = QStringLiteral("%1. Empty %2; tier:<tier> is a tier follow; a preset|model key pins it.")
                             .arg(job.what, follows);
            row.placeholder = follows;
            row.text = relay::rolestore::roleValue(job.role);
            for (const QString &tier : relay::models::curation::tierIds())
                row.completions << QStringLiteral("tier:%1").arg(tier);
            for (const relay::models::Entry &entry : relay::models::shown(catalog))
                row.completions << entry.key;
            row.validator = [role = job.role](const QString &value) {
                return relay::rolestore::roleValueValid(role, value);
            };
            row.onText = [pane, role = job.role](const QString &value) {
                if (!relay::rolestore::applyRoleValue(role, value)) return;
                // The same live update a job rules table edit makes: the served pane's worker is
                // told now, and its next `model_roles` repaints every view of the rules.
                if (pane) pane->rolesChanged();
            };
            models.rows << row;
        }
        models.rows << buttonRow(QStringLiteral("agent.modelRoles"), QStringLiteral("the job rules table"),
                                 QStringLiteral("What each job runs on right now and why — the live report, ranked "
                                                "lists, per-job effort — in the models pane's job rules tab"),
                                 QStringLiteral("job rules…"), [this] { runAction(QStringLiteral("agent.modelRoles")); });
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
        if (!inModelsPane) arranged << original.mid(profilesAt, defaultsAt - profilesAt);
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
            } else if (row.id == QStringLiteral("models.addProvider") || row.id == QStringLiteral("info:models/none")
                       || row.id == QStringLiteral("models.refreshUsage")) {   // #EQH0: above the groups
                flushProvider();
                beforeGroups << row;
            } else {
                providerRows << row; // models link or guest permissions under its provider
            }
        }
        flushProvider();
        arranged << beforeGroups;
        const QStringList labels = inModelsPane
            ? QStringList{QStringLiteral("Coding accounts"), QStringLiteral("Subscription keys"),
                          QStringLiteral("API keys and endpoints")}
            : QStringList{QStringLiteral("Guest Agents"), QStringLiteral("Subscription Keys"),
                          QStringLiteral("Pay-as-you-go Keys")};
        for (int i = 0; i < 3; ++i) {
            // Sources is a short inventory. An empty category need not take up space there;
            // + Add provider remains visible above the groups for first-time setup.
            if (inModelsPane && groups[i].isEmpty()) continue;
            relay::SettingRow head = headingRow(labels.at(i));
            // The heading ID is also the saved collapse key. Keep it when changing the wording.
            if (inModelsPane) {
                const QStringList savedNames{QStringLiteral("Guest Agents"), QStringLiteral("Subscription Keys"),
                                             QStringLiteral("Pay-as-you-go Keys")};
                head.id = QStringLiteral("heading:") + savedNames.at(i);
            }
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
            if (i == 0 && (addClaudeAccount || addCodexAccount))
                arranged << buttonRow(QStringLiteral("models.addGuestAccount"), QStringLiteral("add account"),
                    QStringLiteral("add a separate Claude Code or Codex subscription login"),
                    QStringLiteral("add account…"), [this, addClaudeAccount, addCodexAccount] {
                        QStringList choices;
                        if (addClaudeAccount) choices << QStringLiteral("Claude Code");
                        if (addCodexAccount) choices << QStringLiteral("Codex");
                        bool ok = false;
                        const QString choice = QInputDialog::getItem(this, QStringLiteral("Add account"),
                            QStringLiteral("Which subscription?"), choices, 0, false, &ok);
                        if (!ok) return;
                        if (choice == QStringLiteral("Claude Code")) addClaudeAccount(2);
                        else if (choice == QStringLiteral("Codex")) addCodexAccount(2);
                    });
        }
        arranged << afterGroups;
        if (inModelsPane) arranged << original.mid(profilesAt, defaultsAt - profilesAt);
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
