// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::workspace: a tab's artifact workspace (card #E85D) — which pane is the editor, which the
// console and which the preview, the project root, the source files and the outputs built from
// them — and the typed preview adapters that say how an output is shown and who owns it.
//
// QtCore only, with no pane and no window, so the model, its JSON and the state rules are tested
// on their own (tests/artifactworkspace_test.cpp). RelayWindow (src/RelayWindowWorkspace.cpp) is
// the only caller: it maps leaves onto member ids, applies the layout presets and draws the
// status strip on the preview pane. See docs/ARCHITECTURE.md section 10b.
//
// The vocabulary is the task plugins' and the TeX builder's, deliberately:
//   - roles are `task_plugins.PANE_ROLES` ("editor", "console", "preview", "variables");
//   - a group's kind is a plugin id ("relay.tex") or "plain";
//   - layouts are the manifests' column weights ("1:1:1", "2:1");
//   - output states are `tex_build.STATES` ("idle", "building", "live", "stale", "failed"), and a
//     source revision is `tex_build.source_revision()`, byte for byte, so a revision the builder
//     reports can be compared with one this side computes.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace relay::workspace {

// The saved group's "schema". A group written by a newer Relay is not read (the tab restores
// without it) rather than half-understood. Schema 2 added the member chain (#R660): "members"
// became an ordered array with each member's upstream; schema 1's object shape is still read and
// upgraded to the chain the presets always built (console -> editor -> preview).
inline constexpr int kSchemaVersion = 2;

enum class Role { Editor, Console, Preview, Variables };
QString roleName(Role role);
std::optional<Role> roleFromName(const QString &name);

// Who owns an output's contents. A generated output is read only: it is rebuilt from the sources,
// and an edit made to it would be lost at the next build. An editable one is its own source (a
// Markdown file rendered beside its text) or has a plugin-declared reversible editing model.
enum class Authority { Generated, Editable };
QString authorityName(Authority authority);
Authority authorityFromName(const QString &name, Authority fallback = Authority::Generated);

enum class OutputState { Idle, Building, Live, Stale, Failed };
QString stateName(OutputState state);
std::optional<OutputState> stateFromName(const QString &name);

// One output of the group: a file a preview shows, and which build of it that is.
struct Output {
    QString path;
    QString adapter;                          // an Adapter id ("pdf", "markdown", "image", "text")
    Authority authority = Authority::Generated;
    qint64 generation = 0;                    // 0 until a first output has been seen
    QString sourceRevision;                   // what that generation was built from; "" = unknown
    OutputState state = OutputState::Idle;
    QString signature;                        // "mtime-ms:size" of the file that generation is
    qint64 building = 0;                      // the generation a builder is running, 0 when none
    QString message;                          // a failed build's reason, shown on the strip
    bool builder = false;                     // a builder reports this output (applyBuildStatus)
    qint64 seq = 0;                           // the last builder status taken; older ones drop
};

// What is on disk now, for observe().
struct Observation {
    QString signature;                        // "" when the output file does not exist
    qint64 outputModified = 0;                // ms since the epoch; 0 when missing
    qint64 newestSourceModified = 0;          // the newest source file's, ms since the epoch
    QString revision;                         // sourceRevision() of the sources as saved now
};

// The file-watching rule, for outputs no builder reports on. Returns true when anything changed.
//   - A new signature is a new generation. It is `live` at the current revision when the file is
//     at least as new as every source, and `stale` with an unknown revision when a source was
//     saved after it — an output written before the last save cannot claim to show it.
//   - The same signature: `live` while the revision it was built from is still the one on disk,
//     `stale` once a source is saved differently. The file is not touched, so a stale preview
//     keeps showing the last output until a new one arrives.
//   - `building` and `failed` belong to a builder and are left alone.
// For a builder's output only the live/stale half applies: generations come from its statuses.
bool observe(Output &output, const Observation &now);

// A `tex_build.BuildStatus.to_dict()` (or any builder's status of the same shape):
//   {"state", "seq", "generation": {"id", "revision", ...} | null, "building": id | null,
//    "failed": {"revision", "reason", ...} | null}
// Statuses with a seq not newer than the last one are dropped. A `live` status whose generation
// was built from a revision other than `currentRevision` (when given) is taken as `stale`: the
// preview must never imply it shows sources that were saved after it was built. Returns true when
// the output changed.
bool applyBuildStatus(Output &output, const QJsonObject &status, const QString &currentRevision = QString());

// tex_build.source_revision(): sha256 over the paths (relative to `root`, '/'-separated, sorted)
// and each file's sha256, the first 20 hex digits. A missing file (nullopt) hashes as missing.
QString sourceRevision(const QMap<QString, std::optional<QByteArray>> &contents, const QString &root);
// The same, reading the files from disk. Files over 64 MiB hash by their size and mtime instead
// of their bytes, so a stray build product in the sources cannot stall the window.
QString sourceRevisionOnDisk(const QStringList &paths, const QString &root);
// "mtime-ms:size", or "" when `path` is not a file.
QString fileSignature(const QString &path);

// A tab's workspace group.
struct Group {
    QString id;
    QString kind = QStringLiteral("plain");
    QString root;
    QString layout;                           // the last preset applied ("1:1:1", "2:1"), or ""
    QMap<QString, Role> members;              // member id (a leaf's saved id) -> role
    QStringList order;                        // the chain: member ids head-first, in join order
    QStringList sources;                      // absolute paths, in the order they joined
    QList<Output> outputs;
    // Members that left (their pane closed, or moved to another tab) this run, so a pane restored
    // with Ctrl+Shift+Z — which comes back with its member id — takes its role back. Not saved.
    QHash<QString, Role> departed;

    bool isValid() const { return !id.isEmpty(); }
    static QString newId();                   // a short random id for a group or a member

    // The member holding `role`, or "". A role has at most one member.
    QString memberFor(Role role) const;
    std::optional<Role> roleOf(const QString &member) const;
    // Give `member` the role, taking it from whoever held it (who then leaves the group).
    void setMember(const QString &member, Role role);
    void removeMember(const QString &member);
    // The chain (#R660): `order` head-first. The head is the chain's origin — the shell the
    // editor and preview were opened from; `upstream` walks toward it, `downstream` away.
    QString head() const;
    QString upstreamOf(const QString &member) const;
    QString downstreamOf(const QString &member) const;
    // Drop members that are not among `live` (they are remembered in `departed`) and take back
    // departed ones that are live again, unless their role has been given to someone else.
    // Returns true when membership changed.
    bool reconcile(const QSet<QString> &live);

    bool addSource(const QString &path);
    bool hasSource(const QString &path) const;
    Output *output(const QString &path);
    const Output *output(const QString &path) const;
    // The output at `path`, created with the adapter's id and authority if it is not there yet.
    Output &ensureOutput(const QString &path, const QString &adapter, Authority authority);

    QJsonObject toJson() const;
    // An invalid group (no id) when `json` is empty, not an object this understands, or written
    // with a newer schema; `error` says which.
    static Group fromJson(const QJsonObject &json, QString *error = nullptr);
};

// ----- typed preview adapters -----------------------------------------------------------------
//
// An adapter names the kind of artifact a preview renders and who is authoritative for it. The
// preview pane itself still picks its viewer by MIME type (relay::FilePreview); the adapter is what
// the workspace records about the output and what the status strip says. A plugin registers its
// own (a diagram, a plot, a table, CAD) with `pluginKind` set, and wins over the generic adapter
// for the same extension in its own groups only — nothing about pane-group persistence changes.
struct Adapter {
    QString id;                               // "pdf", "markdown", "image", "text", …
    QString label;                            // "PDF", "Markdown", …
    QStringList extensions;                   // lower case, without the dot
    QStringList mimeTypes;                    // exact ("application/pdf") or "image/*"
    QString pluginKind;                       // "" = any group
    Authority authority = Authority::Generated;
    bool canEdit = false;                     // a visual editor may write it back
    bool available = true;                    // false: this build cannot render it (PDF without Qt PDF)
    QString unavailableReason;
};

class AdapterRegistry {
public:
    // With the built-in adapters: markdown, image, pdf, text.
    AdapterRegistry();
    static AdapterRegistry &instance();

    // Adds `adapter`, replacing one with the same id and plugin kind.
    void add(const Adapter &adapter);
    // The adapter for `path` in a group of `pluginKind`: that plugin's own first, then a generic
    // one; by extension, then by `mime` when given. nullptr when nothing claims it.
    const Adapter *resolve(const QString &path, const QString &pluginKind = QString(),
                           const QString &mime = QString()) const;
    // resolve(), falling back to the "text" adapter.
    const Adapter *adapterFor(const QString &path, const QString &pluginKind = QString(),
                              const QString &mime = QString()) const;
    const Adapter *byId(const QString &id, const QString &pluginKind = QString()) const;
    const QList<Adapter> &adapters() const { return m_adapters; }

private:
    QList<Adapter> m_adapters;
};

// Whether this build renders PDF itself (Qt PDF). Without it the pdf adapter is unavailable and
// the preview offers "Open externally".
bool pdfPreviewBuiltIn();

// The group kind a source file suggests: "relay.tex" for .tex/.ltx, else "plain".
QString kindForSource(const QString &source);
// Where a source's output is expected before any builder says otherwise: a .tex's PDF beside it
// (latexmk's default), otherwise the source itself (a Markdown or image file previews itself).
QString defaultOutputFor(const QString &source);

// ----- layout presets -------------------------------------------------------------------------
//
// A preset is the manifests' column weights. The roles go into the columns as the owner asked
// (card #E85D): three columns are console | editor | preview; two are the editor over the console
// on the left and the preview on the right; one stacks editor, console, preview. Empty for a
// string that is not a preset.
QList<int> presetWeights(const QString &layout);
QList<QList<Role>> presetColumns(const QString &layout);
// `total` shared out by `weights`, the remainder to the first entries, summing to `total`.
QList<int> weightedSizes(const QList<int> &weights, int total);

}  // namespace relay::workspace
