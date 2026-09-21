// SPDX-License-Identifier: AGPL-3.0-or-later
// The model catalog behind the picker and the Models page (owner, 2026-09-20): entries from the
// worker's preset rows, what the user checks and ranks, the sorts, and the words the limits print.
#include "ModelCatalog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::models;

namespace {

// A `models` row as the worker sends it since card #MDL1: `name` is what a person reads, and the
// worker sends `label` equal to it. The tests give the name the worker would.
QJsonObject model(const QString &id, const QString &name, const QString &tier, const QStringList &efforts, int intelligence = -1) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                    {QStringLiteral("label"), name}, {QStringLiteral("tier"), tier},
                    {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
    if (intelligence >= 0) row.insert(QStringLiteral("intelligence"), intelligence);
    return row;
}

// The same row from a worker that predates `name`: the GUI has to derive it (`nameOf`).
QJsonObject oldModel(const QString &id, const QString &tier, const QStringList &efforts) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("tier"), tier},
            {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
}

QJsonObject preset(const QString &id, const QString &label, const QString &provider, const QString &own,
                   const QJsonArray &models, bool key) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("provider"), provider},
            {QStringLiteral("plan"), QStringLiteral("coding plan")}, {QStringLiteral("model"), own},
            {QStringLiteral("models"), models}, {QStringLiteral("has_stored_key"), key},
            {QStringLiteral("efforts"), QJsonArray{QStringLiteral("low"), QStringLiteral("high")}}};
}

// Two keyed providers with a catalog each, one without a key, a guest with its own list, and a
// local server whose probe listed nothing.
QJsonArray presets() {
    QJsonArray out;
    out << preset(QStringLiteral("glm-coding"), QStringLiteral("z.ai · glm-5.3 · coding plan"), QStringLiteral("z.ai (glm)"),
                  QStringLiteral("glm-5.3"),
                  {model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 45),
                   model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3-flash"), QStringLiteral("flash"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 30)},
                  true);
    out << preset(QStringLiteral("kimi-code"), QStringLiteral("kimi code · k3"), QStringLiteral("kimi"), QStringLiteral("k3"),
                  {model(QStringLiteral("k3"), QStringLiteral("kimi-k3"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 44),
                   model(QStringLiteral("kimi-for-coding-highspeed"), QStringLiteral("kimi-for-coding-highspeed"), QStringLiteral("flash"), {})},
                  true);
    out << preset(QStringLiteral("openai"), QStringLiteral("openai · gpt-6 astra"), QStringLiteral("openai (chatgpt)"), QStringLiteral("gpt-6-astra"),
                  {model(QStringLiteral("gpt-6-astra"), QStringLiteral("gpt-6-astra"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}, 53)},
                  false);
    QJsonObject guest{{QStringLiteral("id"), QStringLiteral("guest:claude")}, {QStringLiteral("label"), QStringLiteral("Claude Code")},
                      {QStringLiteral("provider"), QStringLiteral("Claude Code")}, {QStringLiteral("model"), QString()},
                      {QStringLiteral("harness"), true},
                      {QStringLiteral("models"), QJsonArray{model(QStringLiteral("opus"), QStringLiteral("claude-opus-5"), QString(), {QStringLiteral("low"), QStringLiteral("high")}),
                                                            model(QStringLiteral("sonnet"), QStringLiteral("claude-sonnet-5"), QString(), {})}},
                      {QStringLiteral("limits"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("5h")}, {QStringLiteral("used_percent"), 38.0}, {QStringLiteral("resets_at"), 0}},
                                                            QJsonObject{{QStringLiteral("kind"), QStringLiteral("weekly")}, {QStringLiteral("used_percent"), 60.0}, {QStringLiteral("resets_at"), 0}}}}};
    out << guest;
    QJsonObject local{{QStringLiteral("id"), QStringLiteral("local:spark")}, {QStringLiteral("label"), QStringLiteral("spark · bonsai")},
                      {QStringLiteral("provider"), QStringLiteral("spark")}, {QStringLiteral("model"), QStringLiteral("bonsai-2-27b")},
                      {QStringLiteral("local"), true}, {QStringLiteral("models"), QJsonArray()}};
    out << local;
    return out;
}


// ----- one name, one row (card #MDL1) ----------------------------------------------------------
// The same three models served several ways: gpt-5.6-sol through Codex, the OpenAI API and
// OpenRouter; glm-5.3 on a coding plan and on the metered API; and a bonsai the local box serves
// and OpenRouter also lists. Relay Free is there to be ranked last.
QJsonObject provider(const QString &id, const QString &providerName, const QString &plan,
                     const QJsonArray &models, bool usable) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("label"), id}, {QStringLiteral("provider"), providerName},
                    {QStringLiteral("plan"), plan}, {QStringLiteral("model"), QString()},
                    {QStringLiteral("models"), models}};
    if (id.startsWith(QStringLiteral("guest:"))) row.insert(QStringLiteral("harness"), usable);
    else row.insert(QStringLiteral("has_stored_key"), usable);
    return row;
}

QJsonArray groupPresets() {
    QJsonArray out;
    out << provider(QStringLiteral("guest:codex"), QStringLiteral("codex"), QString(),
                    {model(QStringLiteral("gpt-5.6-sol"), QStringLiteral("gpt-5.6-sol"), QString(), {QStringLiteral("low")}),
                     model(QStringLiteral("gpt-6-astra"), QStringLiteral("gpt-6-astra"), QString(), {QStringLiteral("low")})},
                    true);
    out << provider(QStringLiteral("glm-coding"), QStringLiteral("z.ai (glm)"), QStringLiteral("coding plan"),
                    {model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"), {QStringLiteral("high")})},
                    true);
    out << provider(QStringLiteral("glm"), QStringLiteral("z.ai (glm)"), QStringLiteral("standard api"),
                    {model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"), {QStringLiteral("high")})},
                    true);
    out << provider(QStringLiteral("openai"), QStringLiteral("openai (chatgpt)"), QStringLiteral("pay-as-you-go"),
                    {model(QStringLiteral("gpt-5.6-sol"), QStringLiteral("gpt-5.6-sol"), QString(), {QStringLiteral("low")})},
                    true);
    // A worker that predates `name`: the GUI derives "gpt-5.6-sol" off the slug and the row folds in.
    out << provider(QStringLiteral("openrouter"), QStringLiteral("openrouter"), QStringLiteral("pay-as-you-go"),
                    {oldModel(QStringLiteral("openai/gpt-5.6-sol"), QString(), {QStringLiteral("low")}),
                     oldModel(QStringLiteral("prism-ml/bonsai-2-27b"), QString(), {})},
                    true);
    QJsonObject local{{QStringLiteral("id"), QStringLiteral("local:spark")}, {QStringLiteral("label"), QStringLiteral("spark")},
                      {QStringLiteral("provider"), QStringLiteral("spark")}, {QStringLiteral("model"), QStringLiteral("bonsai-2-27b")},
                      {QStringLiteral("plan"), QStringLiteral("llama.cpp")},
                      {QStringLiteral("local"), true}, {QStringLiteral("models"), QJsonArray()}};
    out << local;
    QJsonObject free{{QStringLiteral("id"), QStringLiteral("relay-free")}, {QStringLiteral("label"), QStringLiteral("relay free")},
                     {QStringLiteral("provider"), QStringLiteral("relay")}, {QStringLiteral("plan"), QStringLiteral("included")},
                     {QStringLiteral("hosted"), true}, {QStringLiteral("available"), true},
                     {QStringLiteral("model"), QStringLiteral("relay-main")},
                     {QStringLiteral("models"), QJsonArray{model(QStringLiteral("gpt-5.6-sol"), QStringLiteral("gpt-5.6-sol"), QString(), {}),
                                                           model(QStringLiteral("relay-main"), QStringLiteral("relay-main"), QStringLiteral("main"), {})}}};
    out << free;
    return out;
}

// Every entry of the catalog, in the worker's own order: `grouped` takes whatever list the caller
// has, and these tests want a list nothing else has reordered.
QList<Entry> allEntries(const Catalog &catalog) { return catalog.entries; }

QStringList keysOf(const QList<Entry> &entries) {
    QStringList out;
    for (const Entry &entry : entries) out << entry.key;
    return out;
}

QStringList namesOf(const QList<Group> &groups) {
    QStringList out;
    for (const Group &group : groups) out << group.name;
    return out;
}

// By value: the caller keeps it in a named local, so the pointers `Group::via` hands back outlive
// the list `grouped` returned. An unnamed group has an empty name.
Group groupNamed(const QList<Group> &groups, const QString &name) {
    for (const Group &group : groups) if (group.name == name) return group;
    return Group();
}

}  // namespace

class ModelCatalogTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() { QSettings().clear(); }

    void everyModelOfEveryPresetIsAnEntry() {
        const Catalog catalog = catalogFrom(presets());
        QCOMPARE(catalog.entries.size(), 8);   // 2 + 2 + 1 + 2 + 1 (the local row's own model)
        const Entry *flash = catalog.find(QStringLiteral("glm-coding|glm-5.3-flash"));
        QVERIFY(flash);
        QCOMPARE(flash->name, QStringLiteral("glm-5.3-flash"));
        QCOMPARE(flash->label, flash->name);   // the label is the name now (#MDL1)
        QCOMPARE(flash->provider, QStringLiteral("z.ai (glm)"));
        QCOMPARE(flash->tier, QStringLiteral("flash"));
        QCOMPARE(flash->efforts, (QStringList{QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}));
        QCOMPARE(flash->intelligence, 30);
        QVERIFY(flash->usable);
        QCOMPARE(flash->displayName(), QStringLiteral("glm-5.3-flash · z.ai (glm)"));
        // A provider without a key is in the catalog (the Models page lists it) but not usable.
        QVERIFY(!catalog.find(QStringLiteral("openai|gpt-6-astra"))->usable);
        // Guest rows are lower-cased here even though the worker sent them capitalised.
        const Entry *opus = catalog.find(QStringLiteral("guest:claude|opus"));
        QVERIFY(opus && opus->guest && opus->usable);
        QCOMPARE(opus->provider, QStringLiteral("claude code"));
        // A local server with no probe list still has its own model as an entry.
        const Entry *local = catalog.find(QStringLiteral("local:spark|bonsai-2-27b"));
        QVERIFY(local && local->local && local->usable);
        QVERIFY(local->efforts.isEmpty());   // the row reported no levels: no knob
    }

    void keysSurviveColonsAndSlashes() {
        QString preset, model;
        QVERIFY(Catalog::splitKey(QStringLiteral("guest:claude|opus"), &preset, &model));
        QCOMPARE(preset, QStringLiteral("guest:claude"));
        QCOMPARE(model, QStringLiteral("opus"));
        QVERIFY(Catalog::splitKey(QStringLiteral("openrouter|deepseek/deepseek-v4.1-flash"), &preset, &model));
        QCOMPARE(model, QStringLiteral("deepseek/deepseek-v4.1-flash"));
        QVERIFY(!Catalog::splitKey(QStringLiteral("no-bar"), &preset, &model));
        QVERIFY(!Catalog::splitKey(QStringLiteral("trailing|"), &preset, &model));
    }

    void shownDefaultsToEveryUsableEntry() {
        const Catalog catalog = catalogFrom(presets());
        const QList<Entry> list = shown(catalog);
        QCOMPARE(list.size(), 7);   // openai has no key
        for (const Entry &entry : list) QVERIFY(entry.usable);
    }

    void unCheckingOneMaterialisesTheListAndHidesOnlyThat() {
        const Catalog catalog = catalogFrom(presets());
        curation::setShown(QStringLiteral("kimi-code|kimi-for-coding-highspeed"), false, catalog);
        QCOMPARE(curation::shownKeys().size(), 6);
        QCOMPARE(shown(catalog).size(), 6);
        QVERIFY(!curation::isShown(*catalog.find(QStringLiteral("kimi-code|kimi-for-coding-highspeed"))));
        QVERIFY(curation::isShown(*catalog.find(QStringLiteral("kimi-code|k3"))));
        curation::setShown(QStringLiteral("kimi-code|kimi-for-coding-highspeed"), true, catalog);
        QCOMPARE(shown(catalog).size(), 7);
        // Un-checking everything is a reset, never an empty picker.
        for (const Entry &entry : catalog.entries) curation::setShown(entry.key, false, catalog);
        QVERIFY(curation::shownKeys().isEmpty());
        QCOMPARE(shown(catalog).size(), 7);
    }

    // `shown()` reads the curated list once and hands it to every entry (card #PPR4): asking per
    // entry built a QSettings per entry, which is the 65–81 ms hitch the #PF4K profile measured at
    // the start of a turn. The hoist is only safe while the two forms cannot disagree.
    void theCuratedListIsReadOnceAndBothFormsAgree() {
        const Catalog catalog = catalogFrom(presets());
        curation::setShown(QStringLiteral("kimi-code|kimi-for-coding-highspeed"), false, catalog);
        const QStringList keys = curation::shownKeys();
        QCOMPARE(keys.size(), 6);
        for (const Entry &entry : catalog.entries)
            QCOMPARE(curation::isShown(entry, keys), curation::isShown(entry));
        QCOMPARE(shown(catalog).size(), 6);
        // With no list at all both still mean "every usable entry".
        curation::resetShown();
        const QStringList none = curation::shownKeys();
        QVERIFY(none.isEmpty());
        for (const Entry &entry : catalog.entries) {
            QCOMPARE(curation::isShown(entry, none), entry.usable);
            QCOMPARE(curation::isShown(entry, none), curation::isShown(entry));
        }
        QCOMPARE(shown(catalog).size(), 7);
    }

    void defaultRankPutsTheDefaultProviderFirst() {
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("kimi-code"));
        const Catalog catalog = catalogFrom(presets());
        const QStringList ranks = curation::ranked(catalog);
        QCOMPARE(ranks.at(0), QStringLiteral("kimi-code|k3"));
        QCOMPARE(ranks.at(1), QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        QCOMPARE(ranks.at(2), QStringLiteral("glm-coding|glm-5.3"));   // the next provider's main
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("kimi-code|k3"));
        QCOMPARE(fallback(catalog).key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        QCOMPARE(ranks.size(), catalog.entries.size());
    }

    void movingARankPersistsAndDroppedKeysAreForgotten() {
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("kimi-code"));
        const Catalog catalog = catalogFrom(presets());
        curation::move(QStringLiteral("glm-coding|glm-5.3"), -2, catalog);
        QCOMPARE(curation::ranked(catalog).at(0), QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(fallback(catalog).key, QStringLiteral("kimi-code|k3"));
        curation::setRank(QStringLiteral("guest:claude|opus"), 1, catalog);
        QCOMPARE(fallback(catalog).key, QStringLiteral("guest:claude|opus"));
        // A key that is no longer in the catalog is skipped, not crashed on.
        QStringList stale = curation::priority();
        stale.prepend(QStringLiteral("gone|model"));
        QSettings().setValue(QStringLiteral("models/priority"), stale);
        QCOMPARE(curation::ranked(catalog).at(0), QStringLiteral("glm-coding|glm-5.3"));
        curation::resetPriority();
        QCOMPARE(curation::ranked(catalog).at(0), QStringLiteral("kimi-code|k3"));
    }

    void aCustomModelIsAnEntryOfItsProviderAndShownAtOnce() {
        Catalog catalog = catalogFrom(presets());
        curation::setShown(QStringLiteral("kimi-code|k3"), false, catalog);   // the list is explicit now
        const Entry added = curation::addCustom(QStringLiteral("kimi-code"), QStringLiteral(" k3-256k "), catalog);
        QCOMPARE(added.key, QStringLiteral("kimi-code|k3-256k"));
        QCOMPARE(added.provider, QStringLiteral("kimi"));
        QVERIFY(added.custom && added.usable);
        catalog = catalogFrom(presets());
        const Entry *entry = catalog.find(QStringLiteral("kimi-code|k3-256k"));
        QVERIFY(entry && entry->custom);
        QVERIFY(curation::isShown(*entry));
        curation::removeCustom(entry->key);
        catalog = catalogFrom(presets());
        QVERIFY(!catalog.find(QStringLiteral("kimi-code|k3-256k")));
        QVERIFY(!curation::shownKeys().contains(QStringLiteral("kimi-code|k3-256k")));
    }

    void sortsAreStableOverRank() {
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("glm-coding"));
        const Catalog catalog = catalogFrom(presets());
        const QList<Entry> list = shown(catalog);
        const QList<Entry> alpha = ordered(list, Sort::Alphabetical, catalog);
        QCOMPARE(alpha.first().provider, QStringLiteral("claude code"));
        QCOMPARE(alpha.first().name, QStringLiteral("claude-opus-5"));
        const QList<Entry> smart = ordered(list, Sort::Intelligence, catalog);
        QCOMPARE(smart.first().key, QStringLiteral("glm-coding|glm-5.3"));   // 45 beats 44
        QCOMPARE(smart.at(1).key, QStringLiteral("kimi-code|k3"));
        // Unknown scores sink, in rank order among themselves.
        QCOMPARE(smart.last().intelligence, -1);
        curation::noteUse(QStringLiteral("kimi-code|k3"));
        curation::noteUse(QStringLiteral("kimi-code|k3"));
        curation::noteUse(QStringLiteral("guest:claude|opus"));
        const QList<Entry> used = ordered(list, Sort::Usage, catalog);
        QCOMPARE(used.at(0).key, QStringLiteral("kimi-code|k3"));
        QCOMPARE(used.at(1).key, QStringLiteral("guest:claude|opus"));
        QCOMPARE(curation::recent(), (QStringList{QStringLiteral("guest:claude|opus"), QStringLiteral("kimi-code|k3")}));
        curation::noteSpeed(QStringLiteral("glm-coding|glm-5.3-flash"), 120);
        curation::noteSpeed(QStringLiteral("kimi-code|k3"), 40);
        QCOMPARE(ordered(list, Sort::Speed, catalog).first().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        // Remaining: the guest has 40% left on its weekly window, everyone else has no figure.
        QCOMPARE(ordered(list, Sort::Remaining, catalog).first().preset, QStringLiteral("guest:claude"));
        QCOMPARE(percentLeft(catalog, QStringLiteral("guest:claude")), 40.0);
        QCOMPARE(percentLeft(catalog, QStringLiteral("kimi-code")), -1.0);
    }

    void recentIsCappedAtTen() {
        for (int i = 0; i < 14; ++i) curation::noteUse(QStringLiteral("p|m%1").arg(i));
        QCOMPARE(curation::recent().size(), 10);
        QCOMPARE(curation::recent().first(), QStringLiteral("p|m13"));
        QCOMPARE(curation::uses(QStringLiteral("p|m13")), 1);
    }

    void effortIsRememberedPerEntry() {
        curation::setEffortFor(QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        QCOMPARE(curation::effortFor(QStringLiteral("glm-coding|glm-5.3")), QStringLiteral("max"));
        QCOMPARE(curation::effortFor(QStringLiteral("glm-coding|glm-5.3-flash")), QString());
        curation::setEffortFor(QStringLiteral("glm-coding|glm-5.3"), QString());
        QCOMPARE(curation::effortFor(QStringLiteral("glm-coding|glm-5.3")), QString());
    }

    void openrouterFallbackIsPerModelAndOffByDefault() {
        QVERIFY(curation::openrouterFallbackModels().isEmpty());
        curation::setOpenrouterFallback(QStringLiteral("glm-coding|glm-5.3-flash"), true);
        curation::setOpenrouterFallback(QStringLiteral("glm|glm-5.3-flash"), true);   // the same model on another plan
        curation::setOpenrouterFallback(QStringLiteral("kimi-code|k3"), true);
        QVERIFY(curation::openrouterFallback(QStringLiteral("kimi-code|k3")));
        QVERIFY(!curation::openrouterFallback(QStringLiteral("glm-coding|glm-5.3")));
        QCOMPARE(curation::openrouterFallbackModels(), (QStringList{QStringLiteral("glm-5.3-flash"), QStringLiteral("k3")}));
        curation::setOpenrouterFallback(QStringLiteral("kimi-code|k3"), false);
        QCOMPARE(curation::openrouterFallbackModels(), QStringList{QStringLiteral("glm-5.3-flash")});
    }

    void collapsedProvidersAndTheFallbackThreshold() {
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("kimi-code"));
        const Catalog catalog = catalogFrom(presets());
        QVERIFY(!curation::isCollapsed(QStringLiteral("glm-coding")));
        curation::setCollapsed(QStringLiteral("glm-coding"), true);
        QVERIFY(curation::isCollapsed(QStringLiteral("glm-coding")));
        curation::setCollapsed(QStringLiteral("glm-coding"), false);
        QVERIFY(curation::collapsedProviders().isEmpty());
        // Two above the line by default: Main and one fallback.
        QCOMPARE(curation::fallbackThreshold(), 2);
        QCOMPARE(fallbacks(catalog).size(), 1);
        QCOMPARE(fallbacks(catalog).first().key, fallback(catalog).key);
        curation::setFallbackThreshold(4);
        QCOMPARE(fallbacks(catalog).size(), 3);
        QCOMPARE(fallbacks(catalog).first().key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        curation::setFallbackThreshold(1);   // nothing above the line but Main
        QVERIFY(fallbacks(catalog).isEmpty());
        curation::setFallbackThreshold(99);
        QCOMPARE(fallbacks(catalog).size(), shown(catalog).size() - 1);
    }

    void providerOrderIsAdditionThenDrag() {
        curation::noteProviders({QStringLiteral("glm-coding"), QStringLiteral("relay-free")});
        curation::noteProviders({QStringLiteral("relay-free"), QStringLiteral("kimi-code"), QStringLiteral("glm-coding")});
        QCOMPARE(curation::providerOrder(), (QStringList{QStringLiteral("glm-coding"), QStringLiteral("relay-free"), QStringLiteral("kimi-code")}));
        curation::moveProviderBefore(QStringLiteral("kimi-code"), QStringLiteral("glm-coding"));
        QCOMPARE(curation::providerOrder(), (QStringList{QStringLiteral("kimi-code"), QStringLiteral("glm-coding"), QStringLiteral("relay-free")}));
        curation::moveProviderBefore(QStringLiteral("kimi-code"), QString());
        QCOMPARE(curation::providerOrder().last(), QStringLiteral("kimi-code"));
    }

    void tierListsAreOrderedEntriesWithALevel() {
        const Catalog catalog = catalogFrom(presets());
        QVERIFY(!curation::tierListsSet());
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        curation::addToTier(QStringLiteral("main"), QStringLiteral("kimi-code|k3"), QStringLiteral("high"));
        curation::addToTier(QStringLiteral("main"), QStringLiteral("guest:claude|opus"));
        curation::addToTier(QStringLiteral("main"), QStringLiteral("kimi-code|k3"));   // once only
        QVERIFY(curation::tierListsSet());
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 3);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("max"));
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(fallback(catalog).key, QStringLiteral("kimi-code|k3"));
        QCOMPARE(fallbacks(catalog).size(), 2);                       // every entry after Main, no line
        curation::moveInTier(QStringLiteral("main"), QStringLiteral("guest:claude|opus"), 0);
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("guest:claude|opus"));
        curation::setTierEffort(QStringLiteral("main"), QStringLiteral("kimi-code|k3"), QStringLiteral("low"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).last().effort, QStringLiteral("low"));
        curation::removeFromTier(QStringLiteral("main"), QStringLiteral("guest:claude|opus"));
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(curation::listEffortFor(QStringLiteral("kimi-code|k3")), QStringLiteral("low"));
        QCOMPARE(curation::listEffortFor(QStringLiteral("relay-free|relay-main")), QString());   // in no list: the pane keeps its level
        // The picker's priority order leads with the lists.
        QCOMPARE(curation::ranked(catalog).first(), QStringLiteral("glm-coding|glm-5.3"));
        // An emptied list stays a choice: no fallbacks, and the defaults do not come back.
        curation::setTierList(QStringLiteral("high"), {});
        QVERIFY(curation::tierListsSet());
        QVERIFY(liveTier(catalog, QStringLiteral("high")).isEmpty());
        // The worker's defaults apply as lists.
        curation::applyTierDefaults(QJsonObject{{QStringLiteral("flash"), QJsonArray{QJsonObject{
            {QStringLiteral("preset"), QStringLiteral("glm-coding")}, {QStringLiteral("model"), QStringLiteral("glm-5.3-flash")},
            {QStringLiteral("effort"), QStringLiteral("low")}}}}});
        QCOMPARE(curation::tierList(QStringLiteral("flash")).first().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QVERIFY(curation::tierList(QStringLiteral("main")).isEmpty());   // defaults replace every list
    }

    // Owner, 2026-09-20 evening: "we need model user profiles like warp for the priority lists …
    // an 'AI work' profile and an 'admin work' profile that sets different model priorities."
    void profilesAreNamedSnapshotsOfTheFiveLists() {
        const QString ai = QStringLiteral("AI work"), admin = QStringLiteral("admin work");
        QVERIFY(curation::profiles().isEmpty());
        QCOMPARE(curation::currentProfile(), QString());

        // "AI work": the big models first.
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        curation::addToTier(QStringLiteral("main"), QStringLiteral("kimi-code|k3"), QStringLiteral("high"));
        curation::addToTier(QStringLiteral("flash"), QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low"));
        curation::saveProfile(ai);
        QCOMPARE(curation::profiles(), QStringList{ai});
        QCOMPARE(curation::currentProfile(), ai);

        // "admin work": the cheap ones, saved from the lists as they are, then rewritten.
        curation::saveProfile(admin);
        QCOMPARE(curation::profiles(), (QStringList{ai, admin}));
        QCOMPARE(curation::currentProfile(), admin);
        curation::setTierList(QStringLiteral("main"), {{QStringLiteral("kimi-code|kimi-for-coding-highspeed"), QString()}});
        curation::setTierList(QStringLiteral("flash"), {});

        // Editing a list while a profile is current *is* editing that profile: nothing to save.
        curation::applyProfile(ai);
        QCOMPARE(curation::currentProfile(), ai);
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 2);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("max"));
        QCOMPARE(curation::tierList(QStringLiteral("flash")).first().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        curation::applyProfile(admin);
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 1);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        QVERIFY(curation::tierList(QStringLiteral("flash")).isEmpty());   // a whole snapshot: an empty list stays empty

        // Rank 1 of main follows the profile, which is what a switch is for.
        const Catalog catalog = catalogFrom(presets());
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        curation::applyProfile(ai);
        QCOMPARE(mainDefault(catalog).key, QStringLiteral("glm-coding|glm-5.3"));

        // Storage: one group per profile, and the current name.
        QSettings settings;
        QVERIFY(settings.contains(QStringLiteral("models/profiles/AI work/tier/main")));
        QCOMPARE(settings.value(QStringLiteral("models/profile")).toString(), ai);

        // Rename keeps its place, its lists and (being current) the current name.
        curation::renameProfile(ai, QStringLiteral("deep work"));
        QCOMPARE(curation::profiles(), (QStringList{QStringLiteral("deep work"), admin}));
        QCOMPARE(curation::currentProfile(), QStringLiteral("deep work"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().key, QStringLiteral("glm-coding|glm-5.3"));
        curation::applyProfile(QStringLiteral("deep work"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 2);

        // A name already taken, an empty one and one with a "/" in it are all refused.
        curation::renameProfile(QStringLiteral("deep work"), admin);
        QCOMPARE(curation::profiles(), (QStringList{QStringLiteral("deep work"), admin}));
        curation::renameProfile(QStringLiteral("deep work"), QStringLiteral(" "));
        curation::renameProfile(QStringLiteral("deep work"), QStringLiteral("a/b"));
        QCOMPARE(curation::profiles(), (QStringList{QStringLiteral("deep work"), admin}));
        QVERIFY(!curation::validProfileName(QStringLiteral("a/b")));
        QVERIFY(curation::validProfileName(QStringLiteral("admin work")));

        // Deleting the current profile leaves the lists alone and nothing current.
        curation::deleteProfile(QStringLiteral("deep work"));
        QCOMPARE(curation::profiles(), QStringList{admin});
        QCOMPARE(curation::currentProfile(), QString());
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 2);
        // With no profile current, an edit reaches no profile.
        curation::setTierList(QStringLiteral("main"), {});
        curation::applyProfile(admin);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        // A profile that does not exist is not applied, and is never current.
        curation::applyProfile(QStringLiteral("nobody"));
        QCOMPARE(curation::currentProfile(), admin);
    }

    // Owner, 2026-09-21: "allow exporting and importing profiles."
    void profilesTravelAsJson() {
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        curation::addToTier(QStringLiteral("main"), QStringLiteral("kimi-code|k3"), QStringLiteral("high"));
        curation::addToTier(QStringLiteral("lite"), QStringLiteral("glm-coding|glm-5.3-flash"), QString());
        curation::saveProfile(QStringLiteral("AI work"));
        // A second profile, then an edit: the edit belongs to whichever is current, so "AI work"
        // keeps the two-model main list the file below is checked against.
        curation::saveProfile(QStringLiteral("admin work"));
        curation::setTierList(QStringLiteral("main"), {{QStringLiteral("kimi-code|k3"), QString()}});

        // The file: one object per profile, entries as {preset, model, effort} — the shape the
        // worker's tier_list_defaults already use — and every tier present, empty ones included.
        const QJsonObject document = curation::exportProfiles(curation::profiles());
        QCOMPARE(document.value(QStringLiteral("relay")).toString(), QStringLiteral("model profiles"));
        const QJsonArray written = document.value(QStringLiteral("profiles")).toArray();
        QCOMPARE(written.size(), 2);
        QCOMPARE(written.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("AI work"));
        const QJsonObject lists = written.at(0).toObject().value(QStringLiteral("lists")).toObject();
        QStringList tiers = curation::tierIds();
        tiers.sort();
        QCOMPARE(lists.keys(), tiers);   // QJsonObject keys come back sorted; all five are there
        QCOMPARE(lists.value(QStringLiteral("main")).toArray().at(0).toObject(),
                 (QJsonObject{{QStringLiteral("preset"), QStringLiteral("glm-coding")},
                              {QStringLiteral("model"), QStringLiteral("glm-5.3")},
                              {QStringLiteral("effort"), QStringLiteral("max")}}));
        QVERIFY(lists.value(QStringLiteral("flash")).toArray().isEmpty());
        QVERIFY(curation::exportProfiles(QStringList{QStringLiteral("nobody")}).isEmpty());

        // It survives a round trip through text onto a machine that has never seen it.
        const QJsonObject reread = QJsonDocument::fromJson(QJsonDocument(document).toJson()).object();
        QSettings().clear();
        QVERIFY(curation::profiles().isEmpty());
        QString error = QStringLiteral("untouched");
        const QList<curation::ProfileDoc> incoming = curation::readProfiles(reread, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(incoming.size(), 2);
        for (const curation::ProfileDoc &profile : incoming) curation::writeProfile(profile);
        QCOMPARE(curation::profiles(), (QStringList{QStringLiteral("AI work"), QStringLiteral("admin work")}));
        QCOMPARE(curation::currentProfile(), QString());        // importing runs nothing new
        QVERIFY(curation::tierList(QStringLiteral("main")).isEmpty());
        curation::applyProfile(QStringLiteral("AI work"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 2);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("max"));
        QCOMPARE(curation::tierList(QStringLiteral("lite")).first().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QVERIFY(curation::tierList(QStringLiteral("flash")).isEmpty());

        // Importing over the profile the lists belong to moves the lists with it.
        curation::ProfileDoc replacement;
        replacement.name = QStringLiteral("AI work");
        replacement.lists.insert(QStringLiteral("main"), {{QStringLiteral("kimi-code|k3"), QStringLiteral("low")}});
        curation::writeProfile(replacement);
        QCOMPARE(curation::profiles().size(), 2);               // replaced, not added
        QCOMPARE(curation::tierList(QStringLiteral("main")).size(), 1);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("low"));
        QVERIFY(curation::tierList(QStringLiteral("lite")).isEmpty());   // a whole snapshot again

        // A lone profile object, hand-written, with the key unsplit and no effort.
        const QJsonObject lone{{QStringLiteral("name"), QStringLiteral("borrowed")},
                               {QStringLiteral("lists"), QJsonObject{{QStringLiteral("main"), QJsonArray{
                                   QJsonObject{{QStringLiteral("key"), QStringLiteral("glm-coding|glm-5.3")}}}}}}};
        const QList<curation::ProfileDoc> one = curation::readProfiles(lone, &error);
        QCOMPARE(one.size(), 1);
        QCOMPARE(one.first().lists.value(QStringLiteral("main")).first().key, QStringLiteral("glm-coding|glm-5.3"));
        QVERIFY(one.first().lists.value(QStringLiteral("main")).first().effort.isEmpty());

        // Anything else is refused with a sentence, and nothing is stored.
        QVERIFY(curation::readProfiles(QJsonObject(), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("empty")));
        QVERIFY(curation::readProfiles(QJsonObject{{QStringLiteral("hello"), 1}}, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("not a Relay model-profile file")));
        QVERIFY(curation::readProfiles(QJsonObject{{QStringLiteral("profiles"), QJsonArray{}}}, &error).isEmpty());
        QVERIFY(curation::readProfiles(QJsonObject{{QStringLiteral("relay"), QStringLiteral("model profiles")},
                                                   {QStringLiteral("profiles"), QJsonArray{QJsonObject{
                                                       {QStringLiteral("name"), QStringLiteral("a/b")}}}}}, &error).isEmpty());
        QCOMPARE(curation::profiles().size(), 2);
        // A version from a later Relay is read, not refused: the shape only gains keys.
        QJsonObject newer = reread;
        newer.insert(QStringLiteral("version"), 9);
        QCOMPARE(curation::readProfiles(newer, &error).size(), 2);
    }

    void effortLabelsAreTheProvidersWords() {
        QJsonArray rows = presets();
        QJsonObject openai = rows.at(2).toObject();
        QJsonArray models = openai.value(QStringLiteral("models")).toArray();
        QJsonObject astra = models.at(0).toObject();
        astra.insert(QStringLiteral("effort_labels"), QJsonObject{{QStringLiteral("max"), QStringLiteral("xhigh")}, {QStringLiteral("high"), QStringLiteral("high")}});
        models.replace(0, astra); openai.insert(QStringLiteral("models"), models); rows.replace(2, openai);
        const Catalog catalog = catalogFrom(rows);
        const Entry *entry = catalog.find(QStringLiteral("openai|gpt-6-astra"));
        QVERIFY(entry);
        QCOMPARE(entry->effortLabel(QStringLiteral("max")), QStringLiteral("xhigh"));
        QCOMPARE(entry->effortLabel(QStringLiteral("low")), QStringLiteral("low"));   // no label: the level itself
    }

    // Where a hand-added row starts (card #TKN7). The worker computes it per model (`tier_effort`),
    // because two of its three rules are not readable off a row: Main is the provider's own default,
    // and a codex model's High is `xhigh`, not its top level — `ultra`.
    void aHandAddedModelStartsAtTheLevelTheWorkerNamed() {
        QJsonArray models;
        models << QJsonObject{{QStringLiteral("id"), QStringLiteral("gpt-5.6-sol")},
                              {QStringLiteral("label"), QStringLiteral("GPT-5.6-Sol")},
                              {QStringLiteral("efforts"), QJsonArray::fromStringList(
                                   QStringList{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                                               QStringLiteral("xhigh"), QStringLiteral("max"), QStringLiteral("ultra")})},
                              {QStringLiteral("default_effort"), QStringLiteral("low")},
                              {QStringLiteral("tier_effort"),
                               QJsonObject{{QStringLiteral("main"), QStringLiteral("low")},
                                           {QStringLiteral("high"), QStringLiteral("xhigh")},
                                           {QStringLiteral("flash"), QStringLiteral("low")},
                                           {QStringLiteral("lite"), QStringLiteral("low")}}}};
        QJsonArray rows;
        rows << preset(QStringLiteral("guest:codex"), QStringLiteral("Codex"), QStringLiteral("Codex"),
                       QString(), models, false);
        const Catalog catalog = catalogFrom(rows);
        const Entry *sol = catalog.find(QStringLiteral("guest:codex|gpt-5.6-sol"));
        QVERIFY(sol);
        QCOMPARE(sol->defaultEffort, QStringLiteral("low"));
        QCOMPARE(sol->tierEffort.value(QStringLiteral("high")), QStringLiteral("xhigh"));
        // The report: the button used to take the top of `efforts`, which here is `ultra`.
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("main")), QStringLiteral("low"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("high")), QStringLiteral("xhigh"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("flash")), QStringLiteral("low"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("lite")), QStringLiteral("low"));
    }

    void withoutTheWorkersAnswerTheRowItselfDecides() {
        QJsonArray models;
        models << QJsonObject{{QStringLiteral("id"), QStringLiteral("sol")},
                              {QStringLiteral("efforts"), QJsonArray::fromStringList(
                                   QStringList{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")})},
                              {QStringLiteral("default_effort"), QStringLiteral("medium")}};
        models << QJsonObject{{QStringLiteral("id"), QStringLiteral("no-knob")}, {QStringLiteral("efforts"), QJsonArray()}};
        models << QJsonObject{{QStringLiteral("id"), QStringLiteral("odd-default")},
                              {QStringLiteral("efforts"), QJsonArray::fromStringList(QStringList{QStringLiteral("low"), QStringLiteral("high")})},
                              {QStringLiteral("default_effort"), QStringLiteral("max")}};
        QJsonArray rows;
        rows << preset(QStringLiteral("openai"), QStringLiteral("openai"), QStringLiteral("openai"),
                       QStringLiteral("sol"), models, true);
        const Catalog catalog = catalogFrom(rows);
        const Entry *sol = catalog.find(QStringLiteral("openai|sol"));
        QVERIFY(sol);
        QVERIFY(sol->tierEffort.isEmpty());            // an older worker sends none
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("main")), QStringLiteral("medium"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("high")), QStringLiteral("max"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("flash")), QStringLiteral("low"));
        QCOMPARE(tierStartEffort(*sol, QStringLiteral("lite")), QStringLiteral("low"));
        // A model with no levels has no level to start at, on any list.
        const Entry *plain = catalog.find(QStringLiteral("openai|no-knob"));
        QVERIFY(plain && plain->efforts.isEmpty());
        // A default the model does not offer is not a level to store: the entry carries none and the
        // model's own default applies at run time.
        const Entry *odd = catalog.find(QStringLiteral("openai|odd-default"));
        QVERIFY(odd);
        for (const QString &tier : {QStringLiteral("main"), QStringLiteral("high"), QStringLiteral("flash"), QStringLiteral("lite")})
            QVERIFY(tierStartEffort(*plain, tier).isEmpty());
        QVERIFY(tierStartEffort(*odd, QStringLiteral("main")).isEmpty());
    }

    void anOpenEndedProvidersLongTailIsHiddenUntilChecked() {
        QJsonArray rows = presets();
        QJsonObject big = rows.at(0).toObject();   // glm-coding, given ten models
        QJsonArray models = big.value(QStringLiteral("models")).toArray();
        for (int i = 0; i < 8; ++i) models << model(QStringLiteral("extra-%1").arg(i), QStringLiteral("extra %1").arg(i), QString(), {});
        big.insert(QStringLiteral("models"), models); rows.replace(0, big);
        const Catalog catalog = catalogFrom(rows);
        QVERIFY(catalog.find(QStringLiteral("glm-coding|extra-3"))->openEnded);
        QVERIFY(curation::isShown(*catalog.find(QStringLiteral("glm-coding|glm-5.3"))));         // a tier row
        QVERIFY(!curation::isShown(*catalog.find(QStringLiteral("glm-coding|extra-3"))));        // the tail
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|extra-3"));
        QVERIFY(curation::isShown(*catalog.find(QStringLiteral("glm-coding|extra-3"))));         // named by a list
        curation::setShown(QStringLiteral("glm-coding|extra-5"), true, catalog);
        QVERIFY(curation::isShown(*catalog.find(QStringLiteral("glm-coding|extra-5"))));         // checked by hand
        QVERIFY(!curation::isShown(*catalog.find(QStringLiteral("glm-coding|extra-6"))));        // the rest stays hidden
        QVERIFY(curation::isShown(*catalog.find(QStringLiteral("kimi-code|k3"))));               // a small provider: all in
    }

    void sortRoundTrips() {
        QCOMPARE(curation::sort(), Sort::Priority);
        curation::setSort(Sort::Remaining);
        QCOMPARE(curation::sort(), Sort::Remaining);
        QCOMPARE(sortFromId(QStringLiteral("nonsense")), Sort::Priority);
        for (Sort sort : allSorts()) QCOMPARE(sortFromId(sortId(sort)), sort);
    }

    void limitsTextNamesEachWindow() {
        const qint64 now = QDateTime(QDate(2026, 9, 20), QTime(9, 0)).toSecsSinceEpoch();
        QList<LimitWindow> windows;
        windows << LimitWindow{QStringLiteral("5h"), 38, QDateTime(QDate(2026, 9, 20), QTime(14, 30)).toSecsSinceEpoch()};
        windows << LimitWindow{QStringLiteral("weekly"), 60, QDateTime(QDate(2026, 9, 22), QTime(3, 0)).toSecsSinceEpoch()};
        QCOMPARE(limitsText(windows, now), QStringLiteral("5h 62% left, resets 14:30 · weekly 40% left, resets tue"));
        QCOMPARE(limitsText({}, now), QString());
        QCOMPARE(limitsText({LimitWindow{QStringLiteral("5h"), -1, 0}}, now), QString());
    }

    void theWorkersLimitsObjectIsReadWindowsAndStatus() {
        // The real row (protocol 29.3): `limits: {windows, status?, updated_at}`, not a bare array.
        QJsonArray rows = presets();
        QJsonObject guest = rows.at(3).toObject();
        guest.insert(QStringLiteral("limits"), QJsonObject{
            {QStringLiteral("windows"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("5h")}, {QStringLiteral("used_percent"), 100.0}, {QStringLiteral("resets_at"), 2000}}}},
            {QStringLiteral("status"), QStringLiteral("rejected")}, {QStringLiteral("updated_at"), 1500}});
        rows.replace(3, guest);
        const Catalog catalog = catalogFrom(rows);
        QCOMPARE(catalog.limits.value(QStringLiteral("guest:claude")).size(), 1);
        QCOMPARE(catalog.limits.value(QStringLiteral("guest:claude")).first().usedPercent, 100.0);
        QCOMPARE(catalog.status.value(QStringLiteral("guest:claude")), QStringLiteral("rejected"));
        QVERIFY(catalog.status.value(QStringLiteral("glm-coding")).isEmpty());
    }

    void exhaustedIsASpentWindowUntilItsReset() {
        Catalog catalog = catalogFrom(presets());
        const qint64 now = 1000;
        // 38% and 60% used: live.
        QVERIFY(!exhausted(catalog, QStringLiteral("guest:claude"), now));
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("guest:claude"), now), -1);
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now));   // no figures at all
        // A window at 100% with its reset ahead.
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("5h"), 100, now + 600}};
        QVERIFY(exhausted(catalog, QStringLiteral("kimi-code"), now));
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("kimi-code"), now), now + 600);
        // …and the moment the reset has passed it is live again, with no new report.
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now + 600));
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now + 601));
        // Spent with no reset time: exhausted until the provider says otherwise (until = 0).
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("weekly"), 100, 0}};
        QVERIFY(exhausted(catalog, QStringLiteral("kimi-code"), now));
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("kimi-code"), now), 0);
        // Two spent windows: until the later reset.
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("5h"), 100, now + 100}, LimitWindow{QStringLiteral("weekly"), 100, now + 5000}};
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("kimi-code"), now), now + 5000);
        // 99.6% is not spent: the provider has not refused yet.
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("5h"), 99.6, now + 100}};
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now));
        // The provider's own "rejected" verdict counts even below 100%, until the latest reset it named.
        catalog.status[QStringLiteral("kimi-code")] = QStringLiteral("rejected");
        QVERIFY(exhausted(catalog, QStringLiteral("kimi-code"), now));
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("kimi-code"), now), now + 100);
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now + 100));   // the reset passed: live
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("5h"), 40, 0}};
        QCOMPARE(exhaustedUntil(catalog, QStringLiteral("kimi-code"), now), 0);   // rejected, no reset known
        catalog.status[QStringLiteral("kimi-code")] = QStringLiteral("allowed_warning");
        QVERIFY(!exhausted(catalog, QStringLiteral("kimi-code"), now));
    }

    void thePrioritySkipsAnExhaustedPresetUntilItResets() {
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("kimi-code"));
        curation::setFallbackThreshold(3);
        Catalog catalog = catalogFrom(presets());
        const qint64 now = 1000;
        // Rank: k3, kimi highspeed, glm-5.3, glm flash, opus, sonnet, bonsai.
        QCOMPARE(mainDefault(catalog, now).key, QStringLiteral("kimi-code|k3"));
        QCOMPARE(fallback(catalog, now).key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        catalog.limits[QStringLiteral("kimi-code")] = {LimitWindow{QStringLiteral("weekly"), 100, now + 3600}};
        // Both kimi rows are gone from the live list; the next live ones take their places.
        QCOMPARE(shown(catalog).size(), 7);   // the picker still lists them…
        QCOMPARE(live(catalog, now).size(), 5);   // …the priority does not
        QCOMPARE(mainDefault(catalog, now).key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(fallback(catalog, now).key, QStringLiteral("glm-coding|glm-5.3-flash"));
        const QList<Entry> chain = fallbacks(catalog, now);
        QCOMPARE(chain.size(), 2);
        QCOMPARE(chain.at(0).key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(chain.at(1).key, QStringLiteral("guest:claude|opus"));
        // After the reset, rank 1 is rank 1 again.
        QCOMPARE(mainDefault(catalog, now + 3600).key, QStringLiteral("kimi-code|k3"));
        QCOMPARE(fallbacks(catalog, now + 3600).first().key, QStringLiteral("kimi-code|kimi-for-coding-highspeed"));
        // Everything exhausted: nothing to run on, not a crash.
        for (const QString &preset : catalog.presets()) catalog.limits[preset] = {LimitWindow{QStringLiteral("daily"), 100, 0}};
        QVERIFY(mainDefault(catalog, now).key.isEmpty());
        QVERIFY(fallbacks(catalog, now).isEmpty());
    }

    void resetTextIsTheLimitsLinesWording() {
        const qint64 now = QDateTime(QDate(2026, 9, 20), QTime(9, 0)).toSecsSinceEpoch();
        QCOMPARE(resetText(QDateTime(QDate(2026, 9, 20), QTime(14, 30)).toSecsSinceEpoch(), now), QStringLiteral("14:30"));
        QCOMPARE(resetText(QDateTime(QDate(2026, 9, 22), QTime(3, 0)).toSecsSinceEpoch(), now), QStringLiteral("tue"));
        QCOMPARE(resetText(QDateTime(QDate(2026, 10, 5), QTime(3, 0)).toSecsSinceEpoch(), now), QStringLiteral("5 oct"));
        QCOMPARE(resetText(0, now), QString());
    }


    // ----- one name, one row (card #MDL1) -------------------------------------------------------

    void aModelIsNamedOnceHoweverItIsSpelled() {
        // The design's section 1.2 table, derived (nothing but the id to go on).
        QCOMPARE(nameOf(QStringLiteral("openai/gpt-5.6-sol")), QStringLiteral("gpt-5.6-sol"));
        QCOMPARE(nameOf(QStringLiteral("MiniMax-M3")), QStringLiteral("minimax-m3"));
        QCOMPARE(nameOf(QStringLiteral("minimax/minimax-m3")), QStringLiteral("minimax-m3"));
        QCOMPARE(nameOf(QStringLiteral("anthropic/claude-haiku-4.5")), QStringLiteral("claude-haiku-4.5"));
        QCOMPARE(nameOf(QStringLiteral("z-ai/glm-5.3")), QStringLiteral("glm-5.3"));
        QCOMPARE(nameOf(QStringLiteral("moonshotai/kimi-k3")), QStringLiteral("kimi-k3"));
        // A moving alias keeps its own name, without the "~" OpenRouter writes it with.
        QCOMPARE(nameOf(QStringLiteral("~openai/gpt-sol-latest")), QStringLiteral("gpt-sol-latest"));
        // A serving variant is its own model to the person picking one (design 3.3).
        QCOMPARE(nameOf(QStringLiteral("openai/gpt-5.6-sol:batch")), QStringLiteral("gpt-5.6-sol:batch"));
        QCOMPARE(nameOf(QStringLiteral("k3-256k")), QStringLiteral("k3-256k"));
        // Lower-case, no spaces, whatever was typed.
        QCOMPARE(nameOf(QStringLiteral("  GPT-5.6 Sol  ")), QStringLiteral("gpt-5.6-sol"));
        QCOMPARE(nameOf(QString()), QString());

        const Catalog catalog = catalogFrom(presets());
        // The worker's `name` wins where the id cannot be derived to it.
        QCOMPARE(catalog.find(QStringLiteral("kimi-code|k3"))->name, QStringLiteral("kimi-k3"));
        QCOMPARE(catalog.find(QStringLiteral("guest:claude|opus"))->name, QStringLiteral("claude-opus-5"));
        // A row with no `name` at all (an older worker) is derived here.
        const Catalog groups = catalogFrom(groupPresets());
        QCOMPARE(groups.find(QStringLiteral("openrouter|openai/gpt-5.6-sol"))->name, QStringLiteral("gpt-5.6-sol"));
        // A local endpoint whose probe listed nothing: its own model names the one entry.
        QCOMPARE(groups.find(QStringLiteral("local:spark|bonsai-2-27b"))->name, QStringLiteral("bonsai-2-27b"));
    }

    void oneModelFromThreeProvidersIsOneRow() {
        const Catalog catalog = catalogFrom(groupPresets());
        const QList<Group> groups = grouped(catalog, allEntries(catalog));
        // One group per name, in the order each name first appeared in the list handed in.
        QCOMPARE(namesOf(groups), (QStringList{QStringLiteral("gpt-5.6-sol"), QStringLiteral("gpt-6-astra"),
                                               QStringLiteral("glm-5.3"), QStringLiteral("bonsai-2-27b"),
                                               QStringLiteral("bonsai-2-27b"), QStringLiteral("relay-main")}));
        const Group sol = groupNamed(groups, QStringLiteral("gpt-5.6-sol"));
        QVERIFY(!sol.name.isEmpty());
        // Nothing is ranked yet, so it is the kind of access that decides: the guest harness, then
        // the first-party API, then OpenRouter, then Relay Free.
        QCOMPARE(keysOf(sol.entries), (QStringList{QStringLiteral("guest:codex|gpt-5.6-sol"),
                                                    QStringLiteral("openai|gpt-5.6-sol"),
                                                    QStringLiteral("openrouter|openai/gpt-5.6-sol"),
                                                    QStringLiteral("relay-free|gpt-5.6-sol")}));
    }

    void aRankedEntryComesFirstWhateverItIsServedBy() {
        const Catalog catalog = catalogFrom(groupPresets());
        curation::setTierList(QStringLiteral("main"),
                              {{QStringLiteral("openrouter|openai/gpt-5.6-sol"), QStringLiteral("low")}});
        curation::setTierList(QStringLiteral("flash"),
                              {{QStringLiteral("openai|gpt-5.6-sol"), QString()}});
        const Group sol = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("gpt-5.6-sol"));
        QVERIFY(!sol.name.isEmpty());
        // Main rank 1 first, then the flash list, then the unranked ones in access order.
        QCOMPARE(keysOf(sol.entries), (QStringList{QStringLiteral("openrouter|openai/gpt-5.6-sol"),
                                                    QStringLiteral("openai|gpt-5.6-sol"),
                                                    QStringLiteral("guest:codex|gpt-5.6-sol"),
                                                    QStringLiteral("relay-free|gpt-5.6-sol")}));
    }

    void thePlanIsSpentBeforeTheMeteredApi() {
        const Catalog catalog = catalogFrom(groupPresets());
        const Group glm = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("glm-5.3"));
        QVERIFY(!glm.name.isEmpty());
        // Two presets of one company serving one id: one row, and the coding plan — already paid
        // for — goes first, so the metered "standard api" is only reached when it runs out.
        QCOMPARE(keysOf(glm.entries), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("glm|glm-5.3")}));
    }

    void aLocalModelNeverJoinsACloudRow() {
        const Catalog catalog = catalogFrom(groupPresets());
        const QList<Group> groups = grouped(catalog, allEntries(catalog));
        QList<const Group *> bonsai;
        for (const Group &group : groups) if (group.name == QStringLiteral("bonsai-2-27b")) bonsai << &group;
        QCOMPARE(bonsai.size(), 2);   // "local" is a promise about where the text goes (design 3.2)
        QCOMPARE(keysOf(bonsai.at(0)->entries), QStringList{QStringLiteral("openrouter|prism-ml/bonsai-2-27b")});
        QCOMPARE(keysOf(bonsai.at(1)->entries), QStringList{QStringLiteral("local:spark|bonsai-2-27b")});
    }

    void theRowRunsOnTheFirstProviderThatCanTakeTheTurn() {
        QJsonArray rows = groupPresets();
        // Codex's subscription is spent, and the OpenAI row has no key at all.
        QJsonObject codex = rows.at(0).toObject();
        codex.insert(QStringLiteral("limits"), QJsonObject{{QStringLiteral("windows"), QJsonArray{
            QJsonObject{{QStringLiteral("kind"), QStringLiteral("weekly")}, {QStringLiteral("used_percent"), 100.0},
                        {QStringLiteral("resets_at"), 0}}}}});
        rows.replace(0, codex);
        QJsonObject openai = rows.at(3).toObject();
        openai.insert(QStringLiteral("has_stored_key"), false);
        rows.replace(3, openai);
        const Catalog catalog = catalogFrom(rows);
        const Group sol = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("gpt-5.6-sol"));
        QVERIFY(!sol.name.isEmpty());
        // The order is unchanged — the row does not move when a subscription runs out — but the
        // entry the row runs is the first one that is usable and not exhausted.
        QCOMPARE(sol.entries.first().key, QStringLiteral("guest:codex|gpt-5.6-sol"));
        QCOMPARE(sol.preferred(catalog, 1'000'000).key, QStringLiteral("openrouter|openai/gpt-5.6-sol"));
        QVERIFY(!sol.spent(catalog, 1'000'000));
    }

    void aRowIsGreyOnlyWhenEveryProviderInItIsSpent() {
        QJsonArray rows = groupPresets();
        for (int at : {0, 3, 4, 6}) {          // codex, openai, openrouter, relay free
            QJsonObject row = rows.at(at).toObject();
            row.insert(QStringLiteral("limits"), QJsonObject{{QStringLiteral("windows"), QJsonArray{
                QJsonObject{{QStringLiteral("kind"), QStringLiteral("weekly")}, {QStringLiteral("used_percent"), 100.0},
                            {QStringLiteral("resets_at"), 0}}}}});
            rows.replace(at, row);
        }
        const Catalog catalog = catalogFrom(rows);
        const Group sol = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("gpt-5.6-sol"));
        QVERIFY(!sol.name.isEmpty() && sol.spent(catalog, 1'000'000));
        // Spent is not empty: the row still answers with something to show as the one it would use.
        QCOMPARE(sol.preferred(catalog, 1'000'000).key, QStringLiteral("guest:codex|gpt-5.6-sol"));
        const Group glm = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("glm-5.3"));
        QVERIFY(!glm.name.isEmpty() && !glm.spent(catalog, 1'000'000));
    }

    void viaNamesOneProviderOfTheRow() {
        const Catalog catalog = catalogFrom(groupPresets());
        const Group sol = groupNamed(grouped(catalog, allEntries(catalog)), QStringLiteral("gpt-5.6-sol"));
        QVERIFY(!sol.name.isEmpty());
        QCOMPARE(sol.via(QStringLiteral("openrouter"))->key, QStringLiteral("openrouter|openai/gpt-5.6-sol"));
        QCOMPARE(sol.via(QStringLiteral("codex"))->key, QStringLiteral("guest:codex|gpt-5.6-sol"));
        QCOMPARE(sol.via(QStringLiteral("guest:codex"))->key, QStringLiteral("guest:codex|gpt-5.6-sol"));
        QCOMPARE(sol.via(QStringLiteral("chatgpt"))->key, QStringLiteral("openai|gpt-5.6-sol"));
        QVERIFY(sol.via(QStringLiteral("kimi")) == nullptr);
        QVERIFY(sol.via(QString()) == nullptr);
    }

    void aReportedModelResolvesToThePresetsOwnEntry() {
        const Catalog catalog = catalogFrom(presets());
        // Claude Code is started with the alias `opus` and reports `claude-opus-5`: one entry.
        QCOMPARE(catalog.resolveKey(QStringLiteral("guest:claude"), QStringLiteral("claude-opus-5")),
                 QStringLiteral("guest:claude|opus"));
        QCOMPARE(catalog.resolveKey(QStringLiteral("guest:claude"), QStringLiteral("opus")),
                 QStringLiteral("guest:claude|opus"));
        // An id the preset lists is itself, whatever anything else is called.
        QCOMPARE(catalog.resolveKey(QStringLiteral("glm-coding"), QStringLiteral("glm-5.3-flash")),
                 QStringLiteral("glm-coding|glm-5.3-flash"));
        // A preset with no such entry gets an empty string; no key is invented for the caller.
        QCOMPARE(catalog.resolveKey(QStringLiteral("glm-coding"), QStringLiteral("gpt-6-astra")), QString());
        QCOMPARE(catalog.resolveKey(QStringLiteral("nobody"), QStringLiteral("glm-5.3")), QString());
        QCOMPARE(catalog.resolveKey(QString(), QStringLiteral("glm-5.3")), QString());
        // The same model spelled as OpenRouter spells it still lands on this preset's row.
        const Catalog groups = catalogFrom(groupPresets());
        QCOMPARE(groups.resolveKey(QStringLiteral("openai"), QStringLiteral("openai/gpt-5.6-sol")),
                 QStringLiteral("openai|gpt-5.6-sol"));
    }

    void findByNameTakesTheRowOrOneProviderOfIt() {
        const Catalog catalog = catalogFrom(groupPresets());
        const QList<Entry> rows = allEntries(catalog);
        const Entry *sol = findByName(catalog, rows, QStringLiteral("gpt-5.6-sol"));
        QVERIFY(sol);
        QCOMPARE(sol->key, QStringLiteral("guest:codex|gpt-5.6-sol"));
        const Entry *via = findByName(catalog, rows, QStringLiteral("gpt-5.6-sol@openrouter"));
        QVERIFY(via);
        QCOMPARE(via->key, QStringLiteral("openrouter|openai/gpt-5.6-sol"));
        // A bare model id works too, and so does the spelling another provider uses.
        QVERIFY(findByName(catalog, rows, QStringLiteral("openai/gpt-5.6-sol")));
        QCOMPARE(findByName(catalog, rows, QStringLiteral("GPT-5.6-SOL"))->name, QStringLiteral("gpt-5.6-sol"));
        // A provider that does not serve this model is a miss, not a silent fall back to another.
        QVERIFY(findByName(catalog, rows, QStringLiteral("gpt-5.6-sol@kimi")) == nullptr);
        QVERIFY(findByName(catalog, rows, QStringLiteral("nothing-like-this")) == nullptr);
        QVERIFY(findByName(catalog, rows, QString()) == nullptr);
    }

    void filterMatchesEveryWordAnywhere() {
        const Catalog catalog = catalogFrom(presets());
        const Entry &flash = *catalog.find(QStringLiteral("glm-coding|glm-5.3-flash"));
        QVERIFY(matches(flash, QStringLiteral("flash")));
        QVERIFY(matches(flash, QStringLiteral("GLM flash")));
        QVERIFY(matches(flash, QStringLiteral("z.ai")));
        QVERIFY(matches(flash, QString()));
        QVERIFY(!matches(flash, QStringLiteral("kimi")));
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("relay-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("modelcatalog"));
    ModelCatalogTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "modelcatalog_test.moc"
