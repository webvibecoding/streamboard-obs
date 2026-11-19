// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/game.h"
#include <QFile>
#include <QJsonDocument>
#include "test_support.h"

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void companionKeyCompatibility() {
        QFile file(QStringLiteral(SB_TEST_FIXTURES "/companion-keys.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto vectors = QJsonDocument::fromJson(file.readAll()).array(); QVERIFY(vectors.size() >= 8);
        for(const auto &v : vectors) {
            const auto row = v.toObject(); const auto kind = row["source"].toString();
            const auto actual = kind == "exe" ? sb::executableKey(row["input"].toString()) : kind == "steam" ? sb::steamKey(row["input"].toString()) : sb::epicKey(row["namespace"].toString(),row["item"].toString());
            QCOMPARE(actual,row["expected"].toString());
        }
    }
    void steamFixtures() {
        const auto libraries = sb::steamLibraries(R"vdf("libraryfolders" { "0" { "path" "C:\\Steam" } "1" { "path" "D:\\Games" } })vdf");
        QCOMPARE(libraries,QStringList({"C:\\Steam","D:\\Games"}));
        const auto entry = sb::steamManifest(R"vdf("AppState" { "appid" "730" "name" "Counter-Strike 2" "installdir" "CS2" })vdf","C:/Steam/steamapps");
        QVERIFY(entry); QCOMPARE(entry->game.key,"steam:730"); QCOMPARE(entry->game.name,"Counter-Strike 2");
        QVERIFY(!sb::steamManifest("broken",{}));
    }
    void epicFixtureAndInvalidJson() {
        const auto entry = sb::epicManifest(R"({"CatalogNamespace":"FN","CatalogItemId":"Fortnite","DisplayName":"Fortnite","InstallLocation":"C:/Epic/Fortnite","LaunchExecutable":"FortniteGame/Binaries/game.exe"})");
        QVERIFY(entry); QCOMPARE(entry->game.key,"epic:fn:fortnite");
        QVERIFY(!sb::epicManifest("{")); QVERIFY(!sb::epicManifest("{}"));
    }
    void resolverManualCatalogAndBoundary() {
        const sb::Game game{"steam:730","steam","Counter-Strike 2",{}};
        const QVector<sb::CatalogEntry> catalog{{game,"C:/Games/CS2",{}}};
        const sb::Process process{"cs2.exe","C:/Games/CS2/bin/cs2.exe",{}, {}};
        auto result = sb::resolveGame(process,catalog,{}); QVERIFY(result); QCOMPARE(result->key,"steam:730");
        const auto key = sb::executableKey(process.path);
        result = sb::resolveGame(process,catalog,{{key,"My game"}}); QVERIFY(result); QCOMPARE(result->key,key); QCOMPARE(result->name,"My game");
        result = sb::resolveGame({"game.exe","C:/Games/CS2-other/game.exe",{},{}},catalog,{}); QVERIFY(result); QCOMPARE(result->source,"exe"); QVERIFY(result->needsName);
    }
    void launcherAndObsFiltering() {
        for(const auto &name : {"steam.exe","EpicGamesLauncher.exe","obs64.exe","chrome.exe","explorer.exe"})
            QVERIFY(!sb::resolveGame({name,{},"Product","Game"},{},{}));
    }
    void inaccessibleAndGenericRequireName() {
        auto game = sb::resolveGame({"mystery.exe",{},{},{}},{},{}); QVERIFY(game); QVERIFY(game->needsName); QVERIFY(game->key.startsWith("exe:"));
        game = sb::resolveGame({"game.exe","C:/Games/game.exe","Unity Player","Game"},{},{}); QVERIFY(game); QVERIFY(game->needsName);
        game = sb::resolveGame({"game.exe","C:/Games/game.exe","An Indie Game",{}},{},{}); QVERIFY(game); QVERIFY(!game->needsName);
    }
    void debounceResyncAndAltTab() {
        sb::DetectionState state; const sb::Game game{"steam:730","steam","CS2","cs2.exe"};
        QVERIFY(!state.observe(game,0)); QVERIFY(!state.observe(game,9999)); QVERIFY(state.observe(game,10000));
        QVERIFY(!state.observe(game,39999)); QVERIFY(state.observe(game,40000));
        QVERIFY(!state.observe({},41000)); QVERIFY(!state.observe(game,42000)); QVERIFY(!state.observe(game,50000)); QVERIFY(state.observe(game,52000));
    }
    void gameSwitchPauseResumeAndAcceleration() {
        sb::DetectionState state; const sb::Game a{"steam:1","steam","A","a.exe"}, b{"steam:2","steam","B","b.exe"};
        state.observe(a,0); QVERIFY(state.observe(a,10000)); QVERIFY(!state.observe(b,11000)); QVERIFY(!state.observe(b,20000)); QVERIFY(state.observe(b,21000));
        state.accelerate(); QVERIFY(state.observe(b,22000)); state.reset(); QVERIFY(!state.observe(b,23000)); QVERIFY(!state.observe(b,32999)); QVERIFY(state.observe(b,33000));
    }
    void mappingsGroupSortAndMissingCell() {
        const auto a = sb::Game{"steam:1","steam","Alpha","a.exe"}.json(), z = sb::Game{"exe:key","exe","Zulu","z.exe"}.json();
        const QJsonArray items{QJsonObject{{"game",z},{"platform_id","twitch"},{"category",QJsonObject{{"id","3"},{"name","Z"}}}},
            QJsonObject{{"game",a},{"platform_id","twitch"},{"category",QJsonObject{{"id","1"},{"name","A"}}}},
            QJsonObject{{"game",a},{"platform_id","vkplaylive"},{"category",QJsonObject{{"id","2"},{"name","А"}}}}};
        const auto rows = sb::mappingRows(items); QCOMPARE(rows.size(),2); QCOMPARE(rows[0].game.name,"Alpha");
        QCOMPARE(rows[0].twitch["id"].toString(),"1"); QCOMPARE(rows[0].vk["id"].toString(),"2"); QVERIFY(rows[1].vk.isEmpty());
    }
    void payloadNeverIncludesPathsOrTitles() {
        const auto game = sb::resolveGame({"game.exe","C:/Private/Games/game.exe","Indie Game",{}},{},{}); QVERIFY(game);
        const auto payload = QJsonDocument(game->json()).toJson();
        QVERIFY(!payload.contains("Private")); QVERIFY(!payload.contains("path")); QVERIFY(!payload.contains("title")); QCOMPARE(game->json().size(),4);
    }
};
int runCoreTests(int argc,char **argv) { CoreTests tests; return QTest::qExec(&tests,argc,argv); }
#include "core_tests.moc"
