// SPDX-License-Identifier: AGPL-3.0-or-later
// The model catalog behind the picker and the Models page (owner, 2026-09-20): entries from the
// worker's preset rows, what the user checks and ranks, the sorts, and the words the limits print.
#include "ModelCatalog.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::models;

namespace {

QJsonObject model(const QString &id, const QString &label, const QString &tier, const QStringList &efforts, int intelligence = -1) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("tier"), tier},
                    {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
    if (intelligence >= 0) row.insert(QStringLiteral("intelligence"), intelligence);
    return row;
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
                   model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3 flash"), QStringLiteral("flash"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 30)},
                  true);
    out << preset(QStringLiteral("kimi-code"), QStringLiteral("kimi code · k3"), QStringLiteral("kimi"), QStringLiteral("k3"),
                  {model(QStringLiteral("k3"), QStringLiteral("k3"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 44),
                   model(QStringLiteral("kimi-for-coding-highspeed"), QStringLiteral("kimi for coding highspeed"), QStringLiteral("flash"), {})},
                  true);
    out << preset(QStringLiteral("openai"), QStringLiteral("openai · gpt-6 astra"), QStringLiteral("openai (chatgpt)"), QStringLiteral("gpt-6-astra"),
                  {model(QStringLiteral("gpt-6-astra"), QStringLiteral("gpt-6 astra"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}, 53)},
                  false);
    QJsonObject guest{{QStringLiteral("id"), QStringLiteral("guest:claude")}, {QStringLiteral("label"), QStringLiteral("Claude Code")},
                      {QStringLiteral("provider"), QStringLiteral("Claude Code")}, {QStringLiteral("model"), QString()},
                      {QStringLiteral("harness"), true},
                      {QStringLiteral("models"), QJsonArray{model(QStringLiteral("opus"), QStringLiteral("opus"), QString(), {QStringLiteral("low"), QStringLiteral("high")}),
                                                            model(QStringLiteral("sonnet"), QStringLiteral("sonnet"), QString(), {})}},
                      {QStringLiteral("limits"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("5h")}, {QStringLiteral("used_percent"), 38.0}, {QStringLiteral("resets_at"), 0}},
                                                            QJsonObject{{QStringLiteral("kind"), QStringLiteral("weekly")}, {QStringLiteral("used_percent"), 60.0}, {QStringLiteral("resets_at"), 0}}}}};
    out << guest;
    QJsonObject local{{QStringLiteral("id"), QStringLiteral("local:spark")}, {QStringLiteral("label"), QStringLiteral("spark · bonsai")},
                      {QStringLiteral("provider"), QStringLiteral("spark")}, {QStringLiteral("model"), QStringLiteral("bonsai-2-27b")},
                      {QStringLiteral("local"), true}, {QStringLiteral("models"), QJsonArray()}};
    out << local;
    return out;
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
        QCOMPARE(flash->label, QStringLiteral("glm-5.3 flash"));
        QCOMPARE(flash->provider, QStringLiteral("z.ai (glm)"));
        QCOMPARE(flash->tier, QStringLiteral("flash"));
        QCOMPARE(flash->efforts, (QStringList{QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}));
        QCOMPARE(flash->intelligence, 30);
        QVERIFY(flash->usable);
        QCOMPARE(flash->displayName(), QStringLiteral("glm-5.3 flash · z.ai (glm)"));
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
        QCOMPARE(alpha.first().label, QStringLiteral("opus"));
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
