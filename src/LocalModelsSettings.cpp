// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LocalModelsSettings.h"

namespace relay {

namespace {

const QLatin1String kEndpointsId("lm-endpoints");
const QLatin1String kProbePrefix("lm-ep:");
const QLatin1String kFindPrefix("lm-find:");
const QLatin1String kAddressId("lm-addr");
const QLatin1String kSavePrefix("lm-save:");
const QLatin1String kFindSaveId("lm-find-save");
const QLatin1String kAddressSaveId("lm-addr-save");

SettingRow infoRow(const QString &id, const QString &text) {
    SettingRow row;
    row.kind = SettingRow::Info;
    row.id = id;
    row.label = text;
    return row;
}

SettingRow headingRow(const QString &text) {
    SettingRow row;
    row.kind = SettingRow::Heading;
    row.id = QStringLiteral("heading:") + text;
    row.label = text;
    return row;
}

}  // namespace

QString LocalModelsSettings::baseUrlForPort(int port) {
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

QString LocalModelsSettings::tokensText(int window) {
    if (window <= 0) return QStringLiteral("window unknown");
    QString digits = QString::number(window);
    for (int at = digits.size() - 3; at > 0; at -= 3) digits.insert(at, QLatin1Char(','));
    return digits + QStringLiteral(" tokens");
}

QString LocalModelsSettings::stateWord(const QString &state) {
    if (state == QStringLiteral("ready")) return QStringLiteral("ready");
    if (state == QStringLiteral("loading")) return QStringLiteral("loading");
    if (state == QStringLiteral("sleeping")) return QStringLiteral("sleeping");
    if (state == QStringLiteral("down")) return QStringLiteral("not running");
    return QStringLiteral("checking…");
}

QString LocalModelsSettings::serverLabel(const QString &server) {
    if (server == QStringLiteral("llamacpp")) return QStringLiteral("llama.cpp");
    if (server == QStringLiteral("ollama")) return QStringLiteral("Ollama");
    if (server == QStringLiteral("lmstudio")) return QStringLiteral("LM Studio");
    if (server == QStringLiteral("vllm")) return QStringLiteral("vLLM");
    return QStringLiteral("OpenAI-compatible server");
}

QStringList LocalModelsSettings::modelIds(const QJsonObject &probe) {
    QStringList ids;
    for (const QJsonValue &value : probe.value(QStringLiteral("models")).toArray())
        ids << value.toObject().value(QStringLiteral("id")).toString();
    ids.removeAll(QString());
    return ids;
}

QJsonObject LocalModelsSettings::modelRow(const QJsonObject &probe, const QString &model) {
    for (const QJsonValue &value : probe.value(QStringLiteral("models")).toArray()) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("id")).toString() == model) return row;
    }
    return {};
}

bool LocalModelsSettings::probeIsUseful(const QJsonObject &probe) {
    return probe.value(QStringLiteral("ok")).toBool() && !modelIds(probe).isEmpty();
}

// ----- talking to the worker ---------------------------------------------------------------------

void LocalModelsSettings::refresh() {
    m_asked = true;
    if (send) send({{"type", "local_endpoints"}, {"id", QString(kEndpointsId)}});
}

void LocalModelsSettings::findServers() {
    m_found.clear();
    m_silent.clear();
    m_findNote.clear();
    m_finding = true;
    for (int port : findPorts())
        if (send) send({{"type", "local_probe"}, {"id", QString(kFindPrefix) + QString::number(port)},
                        {"base_url", baseUrlForPort(port)}});
    if (onChanged) onChanged();
}

void LocalModelsSettings::probeEndpoint(const QJsonObject &endpoint) {
    const QString id = endpoint.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || !send) return;
    send({{"type", "local_probe"}, {"id", QString(kProbePrefix) + id},
          {"base_url", endpoint.value(QStringLiteral("base_url")).toString()}});
}

// A save carries the whole endpoint back, not a patch: the registry replaces the entry, and a key
// it does not know is dropped rather than refused, which is why the row is redrawn from the
// `endpoint` the worker echoes rather than from what was sent.
void LocalModelsSettings::saveEndpoint(QJsonObject endpoint, bool detect, const QString &requestId) {
    QJsonObject request{{"type", "local_endpoint_save"}, {"id", requestId}, {"endpoint", endpoint}};
    if (detect) request.insert(QStringLiteral("detect"), true);
    if (send && send(request)) return;
    const QString id = endpoint.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) m_notes[id] = QStringLiteral("The agent worker is not ready yet.");
    if (onChanged) onChanged();
}

QJsonObject LocalModelsSettings::endpointById(const QString &id) const {
    for (const QJsonValue &value : m_endpoints) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("id")).toString() == id) return row;
    }
    return {};
}

void LocalModelsSettings::putEndpoint(const QJsonObject &endpoint) {
    const QString id = endpoint.value(QStringLiteral("id")).toString();
    for (int i = 0; i < m_endpoints.size(); ++i) {
        if (m_endpoints.at(i).toObject().value(QStringLiteral("id")).toString() != id) continue;
        m_endpoints.replace(i, endpoint);
        return;
    }
    m_endpoints.append(endpoint);
}

void LocalModelsSettings::handleEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    const QString id = event.value(QStringLiteral("id")).toString();
    if (type == QStringLiteral("local_endpoints")) {
        m_endpoints = event.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : m_endpoints) probeEndpoint(value.toObject());
    } else if (type == QStringLiteral("local_probed")) {
        if (id.startsWith(kProbePrefix)) {
            m_probes.insert(id.mid(kProbePrefix.size()), event);
        } else if (id.startsWith(kFindPrefix)) {
            const QString port = id.mid(kFindPrefix.size());
            if (probeIsUseful(event)) {
                Found found;
                found.baseUrl = event.value(QStringLiteral("base_url")).toString();
                found.probe = event;
                found.model = modelIds(event).value(0);
                m_found.append(found);
            } else if (!m_silent.contains(port)) {
                m_silent << port;
            }
        } else if (id == kAddressId) {
            m_draft = event;
            m_draftModel = modelIds(event).value(0);
            m_addNote = probeIsUseful(event) ? QString()
                                             : event.value(QStringLiteral("error")).toString(
                                                   QStringLiteral("Nothing is serving a model at that address."));
        } else {
            return;
        }
    } else if (type == QStringLiteral("local_endpoint_saved")) {
        const QJsonObject endpoint = event.value(QStringLiteral("endpoint")).toObject();
        const QString savedId = endpoint.value(QStringLiteral("id")).toString();
        putEndpoint(endpoint);
        const QJsonObject probe = event.value(QStringLiteral("probe")).toObject();
        if (!probe.isEmpty()) m_probes.insert(savedId, probe);
        m_notes[savedId] = QStringLiteral("Saved.");
        if (id == kFindSaveId) {
            m_found.clear();
            m_silent.clear();
            m_findNote = QStringLiteral("Saved %1.").arg(endpoint.value(QStringLiteral("label")).toString());
        } else if (id == kAddressSaveId) {
            m_draft = {};
            m_draftModel.clear();
            m_address.clear();
            m_addNote = QStringLiteral("Saved %1.").arg(endpoint.value(QStringLiteral("label")).toString());
        }
        if (probe.isEmpty()) probeEndpoint(endpoint);
        if (onPresetsChanged) onPresetsChanged();
    } else if (type == QStringLiteral("local_endpoint_deleted")) {
        const QString gone = event.value(QStringLiteral("endpoint_id")).toString();
        QJsonArray kept;
        for (const QJsonValue &value : m_endpoints)
            if (value.toObject().value(QStringLiteral("id")).toString() != gone) kept.append(value);
        m_endpoints = kept;
        m_probes.remove(gone);
        m_notes.remove(gone);
        if (onPresetsChanged) onPresetsChanged();
    } else if (type == QStringLiteral("key_tested")) {
        const QString preset = event.value(QStringLiteral("preset")).toString();
        if (!preset.startsWith(QStringLiteral("local:"))) return;
        m_notes[preset] = event.value(QStringLiteral("ok")).toBool()
            ? QStringLiteral("Test: answered in %1 ms.").arg(event.value(QStringLiteral("elapsed_ms")).toInt())
            : QStringLiteral("Test failed: %1").arg(event.value(QStringLiteral("error")).toString());
    } else if (type == QStringLiteral("error")) {
        const QString text = event.value(QStringLiteral("text")).toString();
        if (id.startsWith(kSavePrefix)) m_notes[id.mid(kSavePrefix.size())] = text;
        else if (id == kFindSaveId) m_findNote = text;
        else if (id == kAddressSaveId) m_addNote = text;
        else return;
    } else {
        return;
    }
    if (onChanged) onChanged();
}

// ----- the rows ------------------------------------------------------------------------------------

void LocalModelsSettings::addEndpointRows(SettingsSection &into, const QJsonObject &endpoint) {
    const QString id = endpoint.value(QStringLiteral("id")).toString();
    const QJsonObject probe = m_probes.value(id);
    const QString state = probe.isEmpty() ? QString() : probe.value(QStringLiteral("state")).toString();
    QStringList detail;
    detail << serverLabel(endpoint.value(QStringLiteral("server")).toString());
    detail << endpoint.value(QStringLiteral("model")).toString();
    detail << tokensText(endpoint.value(QStringLiteral("context_window")).toInt());
    detail << stateWord(state);
    {
        SettingRow row;
        row.kind = SettingRow::Buttons;
        row.id = id;
        row.label = endpoint.value(QStringLiteral("label")).toString();
        row.detail = detail.join(QStringLiteral(" · "));
        row.aliases = QStringLiteral("local model server endpoint llama ollama ")
                      + endpoint.value(QStringLiteral("base_url")).toString();
        row.buttonTexts = QStringList{QStringLiteral("Test"), QStringLiteral("Refresh"), QStringLiteral("Remove")};
        row.onButton = [this, id, endpoint](int index) {
            if (index == 0) {
                m_notes[id] = QStringLiteral("Testing…");
                if (send && !send({{"type", "test_key"}, {"preset", id}}))
                    m_notes[id] = QStringLiteral("The agent worker is not ready yet.");
            } else if (index == 1) {
                // Re-saved with detect, which is what re-reads the window: it is read when the
                // endpoint is saved, not on every turn, so a server restarted with a different -c
                // needs this.
                m_notes[id] = QStringLiteral("Reading the server…");
                saveEndpoint(endpoint, true, QString(kSavePrefix) + id);
            } else {
                if (send) send({{"type", "local_endpoint_delete"}, {"id", QStringLiteral("lm-delete")},
                                {"endpoint_id", id}});
            }
        };
        into.rows << row;
    }
    // Down: the worker's own sentence, which names the command that starts this kind of server.
    const QString error = probe.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) into.rows << infoRow(id + QStringLiteral("/error"), error);
    const QString note = m_notes.value(id);
    if (!note.isEmpty()) into.rows << infoRow(id + QStringLiteral("/note"), note);
    {
        SettingRow row;
        row.kind = SettingRow::Toggle;
        row.id = id + QStringLiteral("/tool_text_recovery");
        row.label = QStringLiteral("Recover tool calls written as text");
        row.detail = QStringLiteral("Runs a command out of the model's text. Leave off unless this model "
                                    "writes tool calls as text.");
        row.checked = endpoint.value(QStringLiteral("tool_text_recovery")).toBool();
        row.onToggle = [this, id, endpoint](bool on) {
            QJsonObject next = endpoint;
            next.insert(QStringLiteral("tool_text_recovery"), on);
            m_notes.remove(id);
            saveEndpoint(next, false, QString(kSavePrefix) + id);
        };
        into.rows << row;
    }
    {
        // A sibling field (default false). The registry stores the keys it knows and drops the
        // rest, so the row is drawn from what came back: if the backend has not learnt this one
        // yet, the toggle goes back to off and says so.
        SettingRow row;
        row.kind = SettingRow::Toggle;
        row.id = id + QStringLiteral("/tool_arguments_as_object");
        row.label = QStringLiteral("Send tool-call arguments as objects");
        row.detail = QStringLiteral("Needed by Muse Glimmer on llama.cpp.");
        row.checked = endpoint.value(QStringLiteral("tool_arguments_as_object")).toBool();
        row.onToggle = [this, id, endpoint](bool on) {
            QJsonObject next = endpoint;
            next.insert(QStringLiteral("tool_arguments_as_object"), on);
            m_notes.remove(id);
            saveEndpoint(next, false, QString(kSavePrefix) + id);
        };
        into.rows << row;
    }
}

void LocalModelsSettings::addFindRows(SettingsSection &into) {
    into.rows << headingRow(QStringLiteral("Find servers"));
    {
        SettingRow row;
        row.kind = SettingRow::Button;
        row.id = QStringLiteral("local:find");
        row.label = QStringLiteral("Find servers");
        row.detail = QStringLiteral("Asks 127.0.0.1 on 11434, 1234, 8080 and 8000 what they serve. "
                                    "Nothing is contacted until you press it.");
        row.aliases = QStringLiteral("scan discover detect ollama lm studio llama.cpp vllm");
        row.buttonText = QStringLiteral("Find servers");
        row.run = [this] { findServers(); };
        into.rows << row;
    }
    for (int i = 0; i < m_found.size(); ++i) {
        const Found &found = m_found.at(i);
        const QStringList models = modelIds(found.probe);
        const QString key = QStringLiteral("found:%1").arg(i);
        if (models.size() > 1) {
            SettingRow choice;
            choice.kind = SettingRow::Choice;
            choice.id = key + QStringLiteral("/model");
            choice.label = QStringLiteral("Model on %1").arg(found.baseUrl);
            choice.detail = QStringLiteral("%1 models are served here; pick the one to save").arg(models.size());
            choice.options = models;
            choice.optionLabels = models;
            choice.current = found.model;
            choice.onChoose = [this, i](const QString &model) {
                if (i < m_found.size()) m_found[i].model = model;
            };
            into.rows << choice;
        }
        const QJsonObject row = modelRow(found.probe, found.model);
        const int window = row.value(QStringLiteral("context_window")).toInt(
            found.probe.value(QStringLiteral("context_window")).toInt());
        SettingRow save;
        save.kind = SettingRow::Buttons;
        save.id = key;
        save.label = QStringLiteral("%1 · %2").arg(serverLabel(found.probe.value(QStringLiteral("server")).toString()),
                                                   found.baseUrl);
        save.detail = QStringList{found.model, tokensText(window),
                                  stateWord(found.probe.value(QStringLiteral("state")).toString())}
                          .join(QStringLiteral(" · "));
        save.buttonTexts = QStringList{QStringLiteral("Save")};
        save.onButton = [this, found](int) {
            m_findNote = QStringLiteral("Saving %1…").arg(found.model);
            saveEndpoint({{"label", found.model}, {"base_url", found.baseUrl}, {"model", found.model},
                          {"server", found.probe.value(QStringLiteral("server")).toString()}},
                         true, kFindSaveId);
        };
        into.rows << save;
    }
    if (m_finding && m_found.isEmpty() && m_silent.size() == findPorts().size())
        into.rows << infoRow(QStringLiteral("local:find/none"),
                             QStringLiteral("Nothing answered on 11434, 1234, 8080 or 8000."));
    else if (m_finding && !m_silent.isEmpty())
        into.rows << infoRow(QStringLiteral("local:find/silent"),
                             QStringLiteral("Nothing on %1.").arg(m_silent.join(QStringLiteral(", "))));
    if (!m_findNote.isEmpty()) into.rows << infoRow(QStringLiteral("local:find/note"), m_findNote);
}

void LocalModelsSettings::addAddressRows(SettingsSection &into) {
    into.rows << headingRow(QStringLiteral("Add by address"));
    {
        SettingRow row;
        row.kind = SettingRow::Text;
        row.id = QStringLiteral("local:address");
        row.label = QStringLiteral("Server address");
        row.detail = QStringLiteral("Plain http on localhost, 127.0.0.1 or ::1");
        row.placeholder = QStringLiteral("http://127.0.0.1:8080");
        row.text = m_address;
        row.onText = [this](const QString &text) { m_address = text; };
        into.rows << row;
    }
    {
        SettingRow row;
        row.kind = SettingRow::Button;
        row.id = QStringLiteral("local:detect");
        row.label = QStringLiteral("Detect");
        row.detail = QStringLiteral("Asks that address for its kind, its models and the window it was started with");
        row.buttonText = QStringLiteral("Detect");
        row.run = [this] {
            if (m_address.trimmed().isEmpty()) {
                m_addNote = QStringLiteral("Type the server's address first.");
                return;
            }
            m_draft = {};
            m_draftModel.clear();
            m_addNote = QStringLiteral("Asking %1…").arg(m_address.trimmed());
            if (send) send({{"type", "local_probe"}, {"id", QString(kAddressId)}, {"base_url", m_address.trimmed()}});
        };
        into.rows << row;
    }
    if (probeIsUseful(m_draft)) {
        const QStringList models = modelIds(m_draft);
        if (models.size() > 1) {
            SettingRow choice;
            choice.kind = SettingRow::Choice;
            choice.id = QStringLiteral("local:address/model");
            choice.label = QStringLiteral("Model");
            choice.detail = QStringLiteral("%1 models are served here; pick the one to save").arg(models.size());
            choice.options = models;
            choice.optionLabels = models;
            choice.current = m_draftModel;
            choice.onChoose = [this](const QString &model) { m_draftModel = model; };
            into.rows << choice;
        }
        const QJsonObject row = modelRow(m_draft, m_draftModel);
        const int window = row.value(QStringLiteral("context_window")).toInt(
            m_draft.value(QStringLiteral("context_window")).toInt());
        SettingRow save;
        save.kind = SettingRow::Buttons;
        save.id = QStringLiteral("local:address/save");
        save.label = QStringLiteral("%1 · %2").arg(serverLabel(m_draft.value(QStringLiteral("server")).toString()),
                                                   m_draft.value(QStringLiteral("base_url")).toString());
        save.detail = QStringList{m_draftModel, tokensText(window),
                                  stateWord(m_draft.value(QStringLiteral("state")).toString())}
                          .join(QStringLiteral(" · "));
        save.buttonTexts = QStringList{QStringLiteral("Save")};
        save.onButton = [this](int) {
            m_addNote = QStringLiteral("Saving %1…").arg(m_draftModel);
            saveEndpoint({{"label", m_draftModel},
                          {"base_url", m_draft.value(QStringLiteral("base_url")).toString()},
                          {"model", m_draftModel},
                          {"server", m_draft.value(QStringLiteral("server")).toString()}},
                         true, kAddressSaveId);
        };
        into.rows << save;
    }
    if (!m_addNote.isEmpty()) into.rows << infoRow(QStringLiteral("local:address/note"), m_addNote);
}

SettingsSection LocalModelsSettings::section() {
    SettingsSection out;
    out.id = sectionId();
    out.title = QStringLiteral("Local models");
    out.blurb = QStringLiteral("A model server on this machine — llama.cpp, Ollama, LM Studio or vLLM. "
                               "No key, and nothing leaves the machine. Relay never starts or stops one.");
    if (m_endpoints.isEmpty())
        out.rows << infoRow(QStringLiteral("local:empty"),
                            m_asked ? QStringLiteral("No local endpoints yet. Find a server below, or add one by address.")
                                    : QStringLiteral("Reading the registry…"));
    for (const QJsonValue &value : m_endpoints) addEndpointRows(out, value.toObject());
    addFindRows(out);
    addAddressRows(out);
    {
        SettingRow row;
        row.kind = SettingRow::Button;
        row.id = QStringLiteral("agent.localModelSetup");
        row.label = QStringLiteral("Set up a model with the agent…");
        row.detail = QStringLiteral("The agent looks at this machine, picks a model and serves it with you");
        row.aliases = QStringLiteral("help install download gguf llama.cpp ollama skill");
        row.buttonText = QStringLiteral("Ask the agent…");
        row.run = [this] { if (onSetupWithAgent) onSetupWithAgent(); };
        out.rows << row;
    }
    return out;
}

}  // namespace relay
