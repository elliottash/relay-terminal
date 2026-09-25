// SPDX-License-Identifier: AGPL-3.0-or-later
// #WBFM screenshot driver: builds a ModelsPane on fixture presets (the same fixtures as
// tests/modelspane_test.cpp), seeds the high/main/flash pick lists (with a tied rank 1 in "high"),
// and grabs the Pick order and Effort tabs.
//
// Usage: drive <out-dir> <label>        e.g. drive . before
// Run under: xvfb-run -a env XDG_CONFIG_HOME=<tmp> RELAY_WORKSPACE=<tmp> ./drive . before
#include "ModelsPane.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTreeWidget>

#include <functional>

using namespace relay;
using namespace relay::models;

namespace {

QJsonObject model(const QString &id, const QString &label, const QString &tier, const QStringList &efforts,
                  int intelligence = -1) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("tier"), tier},
                    {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
    if (intelligence >= 0) row.insert(QStringLiteral("intelligence"), intelligence);
    return row;
}

QJsonArray presets() {
    QJsonArray out;
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("anthropic")},
                       {QStringLiteral("label"), QStringLiteral("anthropic · claude")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic (claude)")},
                       {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5-5")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("claude-opus-5-5"), QStringLiteral("claude opus 5.5"),
                                  QStringLiteral("main"), {}, 51),
                            model(QStringLiteral("claude-sonnet-4-6"), QStringLiteral("claude sonnet 4.6"),
                                  QStringLiteral("main"),
                                  {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                                   QStringLiteral("max")}, 44),
                            model(QStringLiteral("claude-haiku-4-5"), QStringLiteral("claude haiku 4.5"),
                                  QStringLiteral("flash"), {QStringLiteral("low"), QStringLiteral("high")}, 33)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("glm-coding")},
                       {QStringLiteral("label"), QStringLiteral("z.ai · glm-5.3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("z.ai (glm)")},
                       {QStringLiteral("plan"), QStringLiteral("coding plan")},
                       {QStringLiteral("model"), QStringLiteral("glm-5.3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 45),
                            model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3 flash"),
                                  QStringLiteral("flash"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 30)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openai")},
                       {QStringLiteral("label"), QStringLiteral("openai · gpt")},
                       {QStringLiteral("provider"), QStringLiteral("openai (gpt)")},
                       {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("gpt-5.4")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("gpt-5.4"), QStringLiteral("gpt-5.4"), QStringLiteral("high"),
                                  {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}, 49),
                            model(QStringLiteral("gpt-5.4-mini"), QStringLiteral("gpt-5.4 mini"),
                                  QStringLiteral("flash"),
                                  {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}, 36)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("google")},
                       {QStringLiteral("label"), QStringLiteral("google · gemini")},
                       {QStringLiteral("provider"), QStringLiteral("google (gemini)")},
                       {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("gemini-3.1-pro")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("gemini-3.1-pro"), QStringLiteral("gemini 3.1 pro"),
                                  QStringLiteral("high"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 47)}}};
    return out;
}

std::function<QList<SettingsSection>()> providerSections() {
    return [] {
        SettingsSection models;
        models.id = QStringLiteral("models");
        models.title = QStringLiteral("Models");
        SettingRow key;
        key.kind = SettingRow::Button;
        key.id = QStringLiteral("models.key:anthropic");
        key.label = QStringLiteral("anthropic (claude)");
        key.buttonText = QStringLiteral("add key…");
        key.run = [] {};
        models.rows << key;
        return QList<SettingsSection>{models};
    };
}

void settle(int ms = 120) {
    QCoreApplication::processEvents();
    QThread::msleep(ms);
    QCoreApplication::processEvents();
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));

    const QString outDir = QString::fromLocal8Bit(argc > 1 ? argv[1] : ".");
    const QString label = QString::fromLocal8Bit(argc > 2 ? argv[2] : "shot");
    QDir().mkpath(outDir);

    QSettings().clear();
    QSettings().setValue(QStringLiteral("models/priority"),
                         QStringList{QStringLiteral("anthropic|claude-opus-5-5"),
                                     QStringLiteral("glm-coding|glm-5.3")});

    // Pick lists: "high" ties two models at rank 1 (the draw case), "main" and "flash" are
    // ordinary 1-based orders. Efforts are what the Effort tab's selectors open on.
    curation::setTierList(QStringLiteral("high"),
                          {{QStringLiteral("google|gemini-3.1-pro"), QStringLiteral("high"), 1},
                           {QStringLiteral("openai|gpt-5.4"), QStringLiteral("medium"), 1}});
    curation::setTierList(QStringLiteral("main"),
                          {{QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("max"), 1},
                           {QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("high"), 1},
                           {QStringLiteral("anthropic|claude-sonnet-4-6"), QStringLiteral("medium"), 2}});
    curation::setTierList(QStringLiteral("flash"),
                          {{QStringLiteral("anthropic|claude-haiku-4-5"), QStringLiteral("low"), 1},
                           {QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("high"), 2},
                           {QStringLiteral("openai|gpt-5.4-mini"), QStringLiteral("low"), 3}});

    ModelsPane::Target target;
    target.title = QStringLiteral("relay-terminal");
    target.token = QStringLiteral("pane-1");
    target.catalog = catalogFrom(presets());
    target.currentKey = QStringLiteral("anthropic|claude-opus-5-5");
    target.currentEffort = QStringLiteral("max");
    target.tier = QStringLiteral("main");
    target.now = 1;
    target.listsChanged = [] {};
    target.focusBack = [] {};

    ModelsPane pane(providerSections());
    pane.setTarget(target);
    pane.resize(1150, 780);
    pane.show();
    settle();

    pane.showTab(ModelsPane::prioritiesTab());
    settle();
    if (!pane.grab().save(outDir + QLatin1Char('/') + label + QStringLiteral("-pick-order.png"))) return 1;

    const bool dump = qEnvironmentVariableIsSet("WBFM_DUMP");
    if (dump) {
        QTreeWidget *list = pane.picker()->list();
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *r = list->topLevelItem(i);
            if (r->isHidden()) continue;
            QTextStream(stdout) << i << " rank='" << r->text(0) << "' box='" << r->text(3)
                                << "' model='" << r->text(4) << "' via='" << r->text(5)
                                << "' h=" << r->sizeHint(0).height() << "/" << r->sizeHint(4).height()
                                << " vh=" << list->visualItemRect(r).height()
                                << " col0w=" << list->columnWidth(0)
                                << " hdr0='" << list->headerItem()->text(0) << "'"
                                << " flags=" << Qt::hex << int(r->flags()) << Qt::dec
                                << " bg4=" << r->background(4).color().name()
                                << " bold=" << r->font(4).bold() << "\n";
        }
        QTextStream(stdout) << "---\n";
    }

    pane.showTab(ModelsPane::effortTab());
    settle();
    if (!pane.grab().save(outDir + QLatin1Char('/') + label + QStringLiteral("-effort.png"))) return 2;

    if (dump) {
        QTreeWidget *list = pane.picker()->list();
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *r = list->topLevelItem(i);
            if (r->isHidden()) continue;
            QTextStream(stdout) << i << " rank='" << r->text(0) << "' box='" << r->text(3)
                                << "' model='" << r->text(4) << "' h=" << r->sizeHint(0).height()
                                << "/" << r->sizeHint(4).height()
                                << " vh=" << list->visualItemRect(r).height()
                                << " bg4=" << r->background(4).color().name()
                                << " bold=" << r->font(4).bold() << "\n";
        }
    }

    return 0;
}
