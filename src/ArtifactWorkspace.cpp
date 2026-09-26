// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ArtifactWorkspace.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace relay::workspace {

QString roleName(Role role) {
    switch (role) {
    case Role::Editor: return QStringLiteral("editor");
    case Role::Console: return QStringLiteral("console");
    case Role::Preview: return QStringLiteral("preview");
    case Role::Variables: return QStringLiteral("variables");
    }
    return {};
}

std::optional<Role> roleFromName(const QString &name) {
    for (Role role : {Role::Editor, Role::Console, Role::Preview, Role::Variables})
        if (roleName(role) == name) return role;
    return std::nullopt;
}

QString authorityName(Authority authority) {
    return authority == Authority::Editable ? QStringLiteral("editable") : QStringLiteral("generated");
}

Authority authorityFromName(const QString &name, Authority fallback) {
    if (name == QLatin1String("editable")) return Authority::Editable;
    if (name == QLatin1String("generated")) return Authority::Generated;
    return fallback;
}

QString stateName(OutputState state) {
    switch (state) {
    case OutputState::Idle: return QStringLiteral("idle");
    case OutputState::Building: return QStringLiteral("building");
    case OutputState::Live: return QStringLiteral("live");
    case OutputState::Stale: return QStringLiteral("stale");
    case OutputState::Failed: return QStringLiteral("failed");
    }
    return {};
}

std::optional<OutputState> stateFromName(const QString &name) {
    for (OutputState state : {OutputState::Idle, OutputState::Building, OutputState::Live, OutputState::Stale,
                              OutputState::Failed})
        if (stateName(state) == name) return state;
    return std::nullopt;
}

bool observe(Output &output, const Observation &now) {
    const Output before = output;
    if (!output.builder && !now.signature.isEmpty() && now.signature != output.signature) {
        // A new file: a new generation. Whether it shows the saved sources is read off the clock,
        // because nothing reported what it was built from.
        output.generation += 1;
        output.signature = now.signature;
        output.building = 0;
        output.message.clear();
        if (now.outputModified >= now.newestSourceModified) {
            output.sourceRevision = now.revision;
            output.state = OutputState::Live;
        } else {
            output.sourceRevision.clear();
            output.state = OutputState::Stale;
        }
    } else if (output.state == OutputState::Live || output.state == OutputState::Stale) {
        // The same output: it is current while what it was built from is what is saved now.
        const bool current = !output.sourceRevision.isEmpty() && output.sourceRevision == now.revision;
        output.state = current ? OutputState::Live : OutputState::Stale;
    }
    return output.generation != before.generation || output.state != before.state
           || output.sourceRevision != before.sourceRevision || output.signature != before.signature;
}

bool applyBuildStatus(Output &output, const QJsonObject &status, const QString &currentRevision) {
    const qint64 seq = status.value(QStringLiteral("seq")).toVariant().toLongLong();
    if (output.builder && seq <= output.seq) return false;
    const std::optional<OutputState> named = stateFromName(status.value(QStringLiteral("state")).toString());
    if (!named) return false;
    const Output before = output;
    output.builder = true;
    output.seq = seq;
    OutputState state = *named;
    const QJsonObject generation = status.value(QStringLiteral("generation")).toObject();
    if (!generation.isEmpty()) {
        // The published generation is the last good one; a failure or a build in progress leaves
        // it in place, and so does this.
        output.generation = generation.value(QStringLiteral("id")).toVariant().toLongLong();
        output.sourceRevision = generation.value(QStringLiteral("revision")).toString();
    }
    output.building = status.value(QStringLiteral("building")).toVariant().toLongLong();
    const QJsonObject failed = status.value(QStringLiteral("failed")).toObject();
    output.message = state == OutputState::Failed ? failed.value(QStringLiteral("reason")).toString() : QString();
    if (state == OutputState::Live && !currentRevision.isEmpty() && output.sourceRevision != currentRevision)
        state = OutputState::Stale;
    output.state = state;
    return output.generation != before.generation || output.state != before.state
           || output.sourceRevision != before.sourceRevision || output.building != before.building
           || output.message != before.message || !before.builder;
}

QString sourceRevision(const QMap<QString, std::optional<QByteArray>> &contents, const QString &root) {
    // Keyed as tex_build keys it: the relative path, '/'-separated, sorted by the path as given.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QDir base(root);
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        QString rel = QFileInfo(it.key()).isAbsolute() && !root.isEmpty() ? base.relativeFilePath(it.key()) : it.key();
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        hash.addData(rel.toUtf8());
        hash.addData("\0", 1);
        if (!it.value()) {
            hash.addData("\1missing\0", 9);
        } else {
            hash.addData(QCryptographicHash::hash(*it.value(), QCryptographicHash::Sha256));
        }
    }
    return QString::fromLatin1(hash.result().toHex().left(20));
}

QString sourceRevisionOnDisk(const QStringList &paths, const QString &root) {
    constexpr qint64 kHashLimit = 64 * 1024 * 1024;
    QMap<QString, std::optional<QByteArray>> contents;
    for (const QString &path : paths) {
        QFile file(path);
        const QFileInfo info(path);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly)) { contents.insert(path, std::nullopt); continue; }
        if (info.size() > kHashLimit) { contents.insert(path, fileSignature(path).toUtf8()); continue; }
        contents.insert(path, file.readAll());
    }
    return sourceRevision(contents, root);
}

QString fileSignature(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile()) return {};
    return QStringLiteral("%1:%2").arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
}

// ----- the group --------------------------------------------------------------------------------

QString Group::newId() {
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QString Group::memberFor(Role role) const {
    for (auto it = members.cbegin(); it != members.cend(); ++it)
        if (it.value() == role) return it.key();
    return {};
}

std::optional<Role> Group::roleOf(const QString &member) const {
    const auto it = members.constFind(member);
    if (it == members.cend()) return std::nullopt;
    return it.value();
}

void Group::setMember(const QString &member, Role role) {
    if (member.isEmpty()) return;
    // A same-role member leaving hands the newcomer its place in the chain, so a role change
    // keeps the chain's shape (an editor replaced by a new editor pane stays between the console
    // and the preview).
    QString slot;
    for (auto it = members.begin(); it != members.end();) {
        if (it.value() == role && it.key() != member) {
            const int at = order.indexOf(it.key());
            if (at >= 0) slot = it.key();
            it = members.erase(it);
        } else ++it;
    }
    members.insert(member, role);
    if (!order.contains(member)) {
        const int at = slot.isEmpty() ? -1 : order.indexOf(slot);
        if (at >= 0) order.insert(at, member);
        else order.append(member);
    }
    if (!slot.isEmpty()) {
        const int at = order.indexOf(slot);
        if (at >= 0) order.removeAt(at);
    }
    departed.remove(member);
    for (auto it = departed.begin(); it != departed.end();) {
        if (it.value() == role) it = departed.erase(it);
        else ++it;
    }
}

void Group::removeMember(const QString &member) {
    members.remove(member);
    departed.remove(member);
    order.removeAll(member);
}

QString Group::head() const { return order.isEmpty() ? QString() : order.first(); }

QString Group::upstreamOf(const QString &member) const {
    const int at = order.indexOf(member);
    return at > 0 ? order.at(at - 1) : QString();
}

QString Group::downstreamOf(const QString &member) const {
    const int at = order.indexOf(member);
    return at >= 0 && at + 1 < order.size() ? order.at(at + 1) : QString();
}

bool Group::reconcile(const QSet<QString> &live) {
    bool changed = false;
    for (auto it = members.begin(); it != members.end();) {
        if (live.contains(it.key())) { ++it; continue; }
        departed.insert(it.key(), it.value());
        it = members.erase(it);
        changed = true;
    }
    for (auto it = departed.begin(); it != departed.end();) {
        if (!live.contains(it.key())) { ++it; continue; }
        if (memberFor(it.value()).isEmpty()) {
            members.insert(it.key(), it.value());
            changed = true;
        }
        it = departed.erase(it);
    }
    // The chain drops the members that left and takes back the ones that returned (#R660). A
    // returning member reclaims its place; a newcomer that joins while its role is held keeps
    // the tail. `departed` remembers the order entry, so reconcile() restores both membership
    // and position.
    QSet<QString> keep;
    for (auto it = members.cbegin(); it != members.cend(); ++it) keep.insert(it.key());
    QStringList keptOrder;
    for (const QString &id : order)
        if (keep.contains(id)) keptOrder.append(id);
    // Members that never had an order entry (a group from schema 1 read before this chain, or a
    // member added by hand) go on the tail, in the map's role-first order.
    if (keptOrder.size() < members.size()) {
        const QList<Role> chainOrder = {Role::Console, Role::Editor, Role::Preview, Role::Variables};
        for (Role role : chainOrder) {
            const QString id = memberFor(role);
            if (!id.isEmpty() && !keptOrder.contains(id)) keptOrder.append(id);
        }
    }
    if (keptOrder != order) {
        order = keptOrder;
        changed = true;
    }
    return changed;
}

bool Group::addSource(const QString &path) {
    if (path.isEmpty() || sources.contains(path)) return false;
    sources.append(path);
    return true;
}

bool Group::hasSource(const QString &path) const { return sources.contains(path); }

Output *Group::output(const QString &path) {
    for (Output &out : outputs)
        if (out.path == path) return &out;
    return nullptr;
}

const Output *Group::output(const QString &path) const {
    for (const Output &out : outputs)
        if (out.path == path) return &out;
    return nullptr;
}

Output &Group::ensureOutput(const QString &path, const QString &adapter, Authority authority) {
    if (Output *existing = output(path)) return *existing;
    Output out;
    out.path = path;
    out.adapter = adapter;
    out.authority = authority;
    outputs.append(out);
    return outputs.last();
}

QJsonObject Group::toJson() const {
    if (!isValid()) return {};
    // The chain, head-first: each entry names its upstream, so the array spells the order and the
    // links in one place (#R660).
    QJsonArray memberJson;
    for (const QString &id : order) {
        const auto it = members.constFind(id);
        if (it == members.constEnd()) continue;
        QJsonObject m{{QStringLiteral("id"), id},
                      {QStringLiteral("role"), roleName(it.value())}};
        const QString upstream = upstreamOf(id);
        if (!upstream.isEmpty()) m.insert(QStringLiteral("upstream"), upstream);
        memberJson.append(m);
    }
    // A member that somehow lost its order entry (a hand-edited array) is still a member: it
    // rides the tail rather than vanishing from the saved group.
    for (auto it = members.cbegin(); it != members.cend(); ++it) {
        if (order.contains(it.key())) continue;
        memberJson.append(QJsonObject{{QStringLiteral("id"), it.key()},
                                      {QStringLiteral("role"), roleName(it.value())}});
    }
    QJsonArray outputJson;
    for (const Output &out : outputs) {
        QJsonObject o{{QStringLiteral("path"), out.path},
                      {QStringLiteral("adapter"), out.adapter},
                      {QStringLiteral("authority"), authorityName(out.authority)},
                      {QStringLiteral("generation"), out.generation},
                      {QStringLiteral("source_revision"), out.sourceRevision},
                      {QStringLiteral("state"), stateName(out.state)}};
        if (!out.signature.isEmpty()) o.insert(QStringLiteral("signature"), out.signature);
        if (out.builder) {
            o.insert(QStringLiteral("builder"), true);
            o.insert(QStringLiteral("seq"), out.seq);
        }
        if (out.building > 0) o.insert(QStringLiteral("building"), out.building);
        if (!out.message.isEmpty()) o.insert(QStringLiteral("message"), out.message);
        outputJson.append(o);
    }
    QJsonObject json{{QStringLiteral("schema"), kSchemaVersion},
                     {QStringLiteral("id"), id},
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("root"), root},
                     {QStringLiteral("members"), memberJson},
                     {QStringLiteral("sources"), QJsonArray::fromStringList(sources)},
                     {QStringLiteral("outputs"), outputJson}};
    if (!layout.isEmpty()) json.insert(QStringLiteral("layout"), layout);
    return json;
}

Group Group::fromJson(const QJsonObject &json, QString *error) {
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return Group{};
    };
    if (json.isEmpty()) return fail(QStringLiteral("no workspace group"));
    const int schema = json.value(QStringLiteral("schema")).toInt(0);
    if (schema < 1 || schema > kSchemaVersion)
        return fail(QStringLiteral("workspace group schema %1 is not %2").arg(schema).arg(kSchemaVersion));
    Group group;
    group.id = json.value(QStringLiteral("id")).toString();
    if (group.id.isEmpty()) return fail(QStringLiteral("workspace group has no id"));
    group.kind = json.value(QStringLiteral("kind")).toString(QStringLiteral("plain"));
    if (group.kind.isEmpty()) group.kind = QStringLiteral("plain");
    group.root = json.value(QStringLiteral("root")).toString();
    group.layout = presetWeights(json.value(QStringLiteral("layout")).toString()).isEmpty()
                       ? QString() : json.value(QStringLiteral("layout")).toString();
    const QJsonValue membersValue = json.value(QStringLiteral("members"));
    if (membersValue.isArray()) {
        // Schema 2 (#R660): ordered, each member naming its upstream. The array's order is the
        // chain; an upstream that names anything other than the preceding member (a hand edit,
        // a removed middle) is ignored rather than second-guessed.
        QStringList savedOrder;
        for (const auto &value : membersValue.toArray()) {
            const QJsonObject m = value.toObject();
            const QString id = m.value(QStringLiteral("id")).toString();
            const auto role = roleFromName(m.value(QStringLiteral("role")).toString());
            if (id.isEmpty() || !role) continue;
            group.setMember(id, *role);
            savedOrder.append(id);
        }
        QStringList inGroup;
        for (const QString &id : savedOrder)
            if (group.members.contains(id) && !inGroup.contains(id)) inGroup.append(id);
        for (auto it = group.members.cbegin(); it != group.members.cend(); ++it)
            if (!inGroup.contains(it.key())) inGroup.append(it.key());
        group.order = inGroup;
    } else {
        // Schema 1: {member id: role}. Upgraded to the chain the presets always built:
        // console -> editor -> preview, anything else on the tail.
        const QJsonObject memberJson = membersValue.toObject();
        for (auto it = memberJson.constBegin(); it != memberJson.constEnd(); ++it)
            if (const auto role = roleFromName(it.value().toString()); role && !it.key().isEmpty())
                group.setMember(it.key(), *role);   // a hand-edited duplicate role keeps the last one
        QStringList upgraded;
        const QString console = group.memberFor(Role::Console), editor = group.memberFor(Role::Editor),
                    preview = group.memberFor(Role::Preview);
        for (const QString &id : {console, editor, preview})
            if (!id.isEmpty()) upgraded.append(id);
        for (const QString &id : group.order)
            if (!id.isEmpty() && !upgraded.contains(id)) upgraded.append(id);
        group.order = upgraded;
    }
    for (const auto &source : json.value(QStringLiteral("sources")).toArray())
        group.addSource(source.toString());
    for (const auto &value : json.value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject o = value.toObject();
        Output out;
        out.path = o.value(QStringLiteral("path")).toString();
        if (out.path.isEmpty() || group.output(out.path)) continue;
        out.adapter = o.value(QStringLiteral("adapter")).toString();
        out.authority = authorityFromName(o.value(QStringLiteral("authority")).toString());
        out.generation = std::max<qint64>(0, o.value(QStringLiteral("generation")).toVariant().toLongLong());
        out.sourceRevision = o.value(QStringLiteral("source_revision")).toString();
        out.state = stateFromName(o.value(QStringLiteral("state")).toString()).value_or(OutputState::Idle);
        // A build that was running when Relay quit is not running now: the last good generation
        // is what there is, and whether it is current is observe()'s to say.
        if (out.state == OutputState::Building) out.state = out.generation > 0 ? OutputState::Stale : OutputState::Idle;
        out.signature = o.value(QStringLiteral("signature")).toString();
        out.builder = o.value(QStringLiteral("builder")).toBool();
        out.seq = o.value(QStringLiteral("seq")).toVariant().toLongLong();
        out.message = o.value(QStringLiteral("message")).toString();
        group.outputs.append(out);
    }
    return group;
}

// ----- adapters ---------------------------------------------------------------------------------

bool pdfPreviewBuiltIn() {
#ifdef RELAY_HAVE_QTPDF
    return true;
#else
    return false;
#endif
}

AdapterRegistry::AdapterRegistry() {
    // The views FilePreview already has. Markdown is its own source, rendered: editable. Images
    // and PDFs are what a build or a program produced: generated, and read only here.
    add({QStringLiteral("markdown"), QStringLiteral("Markdown"),
         {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"), QStringLiteral("mkd")},
         {QStringLiteral("text/markdown")}, QString(), Authority::Editable, true, true, QString()});
    add({QStringLiteral("image"), QStringLiteral("Image"),
         {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
          QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("svg")},
         {QStringLiteral("image/*")}, QString(), Authority::Generated, false, true, QString()});
    Adapter pdf{QStringLiteral("pdf"), QStringLiteral("PDF"), {QStringLiteral("pdf")},
                {QStringLiteral("application/pdf")}, QString(), Authority::Generated, false, true, QString()};
    if (!pdfPreviewBuiltIn()) {
        pdf.available = false;
        pdf.unavailableReason = QStringLiteral("This build of Relay has no PDF viewer (Qt PDF).");
    }
    add(pdf);
    add({QStringLiteral("text"), QStringLiteral("Text"), {}, {QStringLiteral("text/*")}, QString(),
         Authority::Editable, true, true, QString()});
}

AdapterRegistry &AdapterRegistry::instance() {
    static AdapterRegistry registry;
    return registry;
}

void AdapterRegistry::add(const Adapter &adapter) {
    for (Adapter &existing : m_adapters)
        if (existing.id == adapter.id && existing.pluginKind == adapter.pluginKind) { existing = adapter; return; }
    m_adapters.append(adapter);
}

const Adapter *AdapterRegistry::resolve(const QString &path, const QString &pluginKind, const QString &mime) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    const auto mimeMatches = [&mime](const Adapter &a) {
        if (mime.isEmpty()) return false;
        for (const QString &m : a.mimeTypes) {
            if (m == mime) return true;
            if (m.endsWith(QLatin1String("/*")) && mime.startsWith(m.chopped(1))) return true;
        }
        return false;
    };
    // The group's own plugin first, then any: by extension, then by MIME type.
    QStringList kinds;
    if (!pluginKind.isEmpty()) kinds << pluginKind;
    kinds << QString();
    for (const QString &kind : kinds) {
        for (const Adapter &a : m_adapters)
            if (a.pluginKind == kind && !ext.isEmpty() && a.extensions.contains(ext)) return &a;
        for (const Adapter &a : m_adapters)
            if (a.pluginKind == kind && mimeMatches(a)) return &a;
    }
    return nullptr;
}

const Adapter *AdapterRegistry::adapterFor(const QString &path, const QString &pluginKind, const QString &mime) const {
    if (const Adapter *found = resolve(path, pluginKind, mime)) return found;
    return byId(QStringLiteral("text"));
}

const Adapter *AdapterRegistry::byId(const QString &id, const QString &pluginKind) const {
    if (!pluginKind.isEmpty())
        for (const Adapter &a : m_adapters)
            if (a.id == id && a.pluginKind == pluginKind) return &a;
    for (const Adapter &a : m_adapters)
        if (a.id == id && a.pluginKind.isEmpty()) return &a;
    return nullptr;
}

QString kindForSource(const QString &source) {
    const QString ext = QFileInfo(source).suffix().toLower();
    if (ext == QLatin1String("tex") || ext == QLatin1String("ltx")) return QStringLiteral("relay.tex");
    return QStringLiteral("plain");
}

QString defaultOutputFor(const QString &source) {
    const QFileInfo info(source);
    const QString ext = info.suffix().toLower();
    if (ext == QLatin1String("tex") || ext == QLatin1String("ltx"))
        return info.dir().filePath(info.completeBaseName() + QStringLiteral(".pdf"));
    return source;
}

// ----- presets ----------------------------------------------------------------------------------

QList<int> presetWeights(const QString &layout) {
    // task_plugins.LAYOUT_RE's shape: positive integers joined by ':'.
    static const QRegularExpression shape(QStringLiteral("^[1-9][0-9]?(:[1-9][0-9]?){0,3}$"));
    if (!shape.match(layout).hasMatch()) return {};
    QList<int> weights;
    for (const QString &part : layout.split(QLatin1Char(':'))) weights.append(part.toInt());
    return weights;
}

QList<QList<Role>> presetColumns(const QString &layout) {
    const int columns = int(presetWeights(layout).size());
    if (columns >= 3) return {{Role::Console}, {Role::Editor}, {Role::Preview}};
    if (columns == 2) return {{Role::Editor, Role::Console}, {Role::Preview}};
    if (columns == 1) return {{Role::Editor, Role::Console, Role::Preview}};
    return {};
}

QList<int> weightedSizes(const QList<int> &weights, int total) {
    qint64 sum = 0;
    for (int w : weights) sum += std::max(0, w);
    if (weights.isEmpty() || sum <= 0 || total <= 0) return {};
    QList<int> sizes;
    int given = 0;
    for (int w : weights) {
        sizes.append(int(qint64(total) * std::max(0, w) / sum));
        given += sizes.last();
    }
    for (int i = 0; given < total; i = (i + 1) % int(sizes.size()), ++given) sizes[i] += 1;
    return sizes;
}

}  // namespace relay::workspace
