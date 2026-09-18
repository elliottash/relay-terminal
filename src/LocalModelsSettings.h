// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Settings › Local models (card #24XJ): the rows that add, check and remove a model server running
// on this machine. It is a section of the Options pane, not a dialog of its own — the owner's line
// (2026-09-18) is that a new surface is a pane or a section of one.
//
// This class owns no widgets. It holds what the worker last said and turns it into a
// SettingsSection, so the pane's row model stays the only way rows are drawn. The caller wires it
// to a pane's worker — the same connection the keys dialog uses, never a second one:
//
//   * `send` carries `local_endpoints`, `local_probe`, `local_endpoint_save`,
//     `local_endpoint_delete` and `test_key` out, and returns false while the worker is starting.
//   * `handleEvent` takes `local_endpoints`, `local_probed`, `local_endpoint_saved`,
//     `local_endpoint_deleted`, a `key_tested` for a `local:` preset and an `error` carrying one of
//     our request ids back (protocol section 23).
//   * `onPresetsChanged` fires after a save or a delete, so every pane re-reads `presets` and its
//     model dropdown gains or loses the row.
//
// Probing is never on a timer: a probe wakes the server's socket, and llama-server's sleep timer
// is the whole point of --sleep-idle-seconds. It happens when the section comes to the front
// (SettingsPane::onSectionShown) and when Refresh or Find servers is pressed.
#include "SettingsPane.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace relay {

class LocalModelsSettings {
public:
    // The worker echoes a request's `id` back on its event, so every answer finds its own row.
    static QString sectionId() { return QStringLiteral("local_models"); }
    // The loopback ports Find servers knocks on: Ollama, LM Studio, llama.cpp, vLLM.
    static QList<int> findPorts() { return {11434, 1234, 8080, 8000}; }
    static QString baseUrlForPort(int port);
    // "131,072 tokens", grouped by hand so the row reads the same whatever the locale is.
    static QString tokensText(int window);
    // ready · loading · sleeping · not running, and "checking…" until a probe has answered.
    static QString stateWord(const QString &state);
    static QString serverLabel(const QString &server);

    std::function<bool(const QJsonObject &request)> send;   // false while the worker is starting
    std::function<void()> onChanged;                        // an event arrived; redraw the section
    std::function<void()> onPresetsChanged;                 // a save or a delete: every pane re-reads presets
    std::function<void()> onSetupWithAgent;                 // "Set up a model with the agent…"

    SettingsSection section();
    // The section came to the front, or Refresh was pressed: read the registry, then probe each
    // endpoint once.
    void refresh();
    void findServers();
    void handleEvent(const QJsonObject &event);

    // Tests read these rather than the widgets.
    QJsonArray endpoints() const { return m_endpoints; }
    QString noteFor(const QString &endpointId) const { return m_notes.value(endpointId); }

private:
    struct Found { QString baseUrl; QJsonObject probe; QString model; };

    void probeEndpoint(const QJsonObject &endpoint);
    void saveEndpoint(QJsonObject endpoint, bool detect, const QString &requestId);
    QJsonObject endpointById(const QString &id) const;
    void putEndpoint(const QJsonObject &endpoint);
    void addEndpointRows(SettingsSection &into, const QJsonObject &endpoint);
    void addFindRows(SettingsSection &into);
    void addAddressRows(SettingsSection &into);
    static QStringList modelIds(const QJsonObject &probe);
    static QJsonObject modelRow(const QJsonObject &probe, const QString &model);
    static bool probeIsUseful(const QJsonObject &probe);

    QJsonArray m_endpoints;                     // `local_endpoints` items, kept in step with saves
    QHash<QString, QJsonObject> m_probes;       // endpoint id → its last `local_probed`
    QHash<QString, QString> m_notes;            // endpoint id → the one line under its row
    QList<Found> m_found;                       // Find servers: one entry per port that answered
    QStringList m_silent;                       // …and the ports that did not
    QJsonObject m_draft;                        // Add by address: the last `local_probed`
    QString m_address, m_draftModel, m_findNote, m_addNote;
    bool m_asked = false;                       // the registry has been read at least once
    bool m_finding = false;
};

}  // namespace relay
