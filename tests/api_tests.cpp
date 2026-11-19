// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/api.h"
#include "core/tracker.h"
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

struct Request { QByteArray method; QUrl url; sb::Headers headers; QJsonObject body; };
class FakeTransport : public sb::Transport {
public:
    QVector<Request> requests;
    std::function<sb::Response(const Request &)> handler;
    void send(const QByteArray &method,const QUrl &url,const sb::Headers &headers,const QJsonObject &body,sb::Reply done) override {
        requests.append({method,url,headers,body}); const auto result = handler(requests.last());
        QTimer::singleShot(0,this,[done = std::move(done),result] { done(result); });
    }
    void cancelAll() override {}
};
class MemoryStore : public sb::CredentialStore {
public:
    std::optional<sb::Credential> value;
    int writes = 0;
    std::optional<sb::Credential> read() override { return value; }
    bool write(const sb::Credential &c) override { value = c; ++writes; return true; }
    bool clear() override { value.reset(); return true; }
    void seed(bool expired = false) { value = sb::Credential{"old-access","old-refresh","device",QDateTime::currentDateTimeUtc().addSecs(expired ? -1 : 900)}; }
};
static QJsonObject tokens() { return {{"access_token","next-access"},{"refresh_token","next-refresh"},{"device_id","device"},{"expires_in",900}}; }
class ApiTests : public QObject {
    Q_OBJECT
private slots:
    void pkceRfc7636AndRandomness() {
        QCOMPARE(sb::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
        const auto a = sb::pkceVerifier(), b = sb::pkceVerifier(); QCOMPARE(a.size(),43); QVERIFY(a != b);
    }
    void browserOriginValidation() {
        QVERIFY(sb::trustedPairingUrl(QUrl("https://streamboard.sip1ay.pro/next/?desktop_pairing=ABCD")));
        for(const auto &url : {"http://streamboard.sip1ay.pro/","https://evil.example/","https://user@streamboard.sip1ay.pro/","https://streamboard.sip1ay.pro:444/"}) QVERIFY(!sb::trustedPairingUrl(QUrl(url)));
    }
    void noCredentialDoesNotPairInBackground() {
        FakeTransport transport; MemoryStore store; sb::Api api(transport,store,"1.0.0");
        sb::Response response; api.request("GET","bootstrap",{},[&](auto r) { response = r; });
        QCOMPARE(response.error,"desktop_pairing_required"); QVERIFY(transport.requests.isEmpty());
    }
    void refreshOnceForConcurrentJobs() {
        FakeTransport transport; MemoryStore store; store.seed(true); int refreshes = 0, completed = 0;
        transport.handler = [&](const Request &r) -> sb::Response {
            if(r.url.path().endsWith("/token")) { ++refreshes; return {200,tokens()}; }
            return {200,{{"ok",true}}};
        };
        sb::Api api(transport,store,"1.0.0");
        api.request("GET","bootstrap",{},[&](auto r) { QVERIFY(r.ok()); ++completed; });
        api.request("GET","mappings",{},[&](auto r) { QVERIFY(r.ok()); ++completed; });
        QTRY_COMPARE(completed,2); QCOMPARE(refreshes,1); QCOMPARE(store.writes,1); QCOMPARE(store.value->refresh,"next-refresh");
        QCOMPARE(transport.requests[0].body["refresh_token"].toString(),"old-refresh");
        QCOMPARE(transport.requests[1].headers.value("Authorization"),QByteArray("Bearer next-access"));
    }
    void device401RefreshesAndRetriesSameEvent() {
        FakeTransport transport; MemoryStore store; store.seed(); int syncs = 0; bool completed = false;
        transport.handler = [&](const Request &r) -> sb::Response {
            if(r.url.path().endsWith("/token")) return {200,tokens()};
            ++syncs; return syncs == 1 ? sb::Response{401,{},"desktop_access_token_invalid"} : sb::Response{200,{}};
        };
        sb::Api api(transport,store,"1.0.0"); const QJsonObject body{{"event_id","stable-event"}};
        api.request("POST","sync",body,[&](auto r) { QVERIFY(r.ok()); completed = true; });
        QTRY_VERIFY(completed); QCOMPARE(syncs,2); QCOMPARE(transport.requests[0].body,transport.requests[2].body);
    }
    void provider401DoesNotRotateDeviceToken() {
        FakeTransport transport; MemoryStore store; store.seed(); bool completed = false;
        transport.handler = [](const Request &) { return sb::Response{401,{},"twitch_token_expired"}; };
        sb::Api api(transport,store,"1.0.0"); api.request("GET","categories",{},[&](auto r) { QCOMPARE(r.error,"twitch_token_expired"); completed = true; });
        QTRY_VERIFY(completed); QCOMPARE(transport.requests.size(),1); QCOMPARE(store.writes,0); QVERIFY(api.connected());
    }
    void revokeRemovesCredential() {
        FakeTransport transport; MemoryStore store; store.seed(); bool completed = false;
        transport.handler = [](const Request &) { return sb::Response{401,{},"desktop_device_revoked"}; };
        sb::Api api(transport,store,"1.0.0"); api.request("GET","bootstrap",{},[&](auto) { completed = true; });
        QTRY_VERIFY(completed); QVERIFY(!store.value); QVERIFY(!api.connected()); QCOMPARE(transport.requests.size(),1);
    }
    void networkFailurePreservesRefreshCredential() {
        FakeTransport transport; MemoryStore store; store.seed(true); bool completed = false;
        transport.handler = [](const Request &) { return sb::Response{0,{},"network_error"}; };
        sb::Api api(transport,store,"1.0.0"); api.request("GET","bootstrap",{},[&](auto r) { QCOMPARE(r.error,"network_error"); completed = true; });
        QTRY_VERIFY(completed); QVERIFY(store.value); QCOMPARE(store.value->refresh,"old-refresh");
    }
    void pairingUsesPkceAndStoresRotatedSession() {
        FakeTransport transport; MemoryStore store; QString challenge;
        transport.handler = [&](const Request &r) -> sb::Response {
            if(r.url.path().endsWith("/pairings")) {
                challenge = r.body["code_challenge"].toString();
                return {201,{{"device_code","device-code"},{"user_code","ABCD-EFGH"},{"verification_uri_complete","https://streamboard.sip1ay.pro/next/?desktop_pairing=ABCD-EFGH"},{"expires_in",600},{"interval",1}}};
            }
            return {200,tokens()};
        };
        sb::Api api(transport,store,"1.0.0"); QSignalSpy finished(&api,&sb::Api::pairingFinished); api.pair("OBS test");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,3000); QVERIFY(api.connected()); QCOMPARE(store.writes,1);
        QCOMPARE(sb::pkceChallenge(transport.requests.last().body["code_verifier"].toString()),challenge);
        QVERIFY(!transport.requests[0].headers.contains("Authorization"));
    }
    void pairingExpiry() {
        FakeTransport transport; MemoryStore store;
        transport.handler = [](const Request &) { return sb::Response{201,{{"device_code","device-code"},{"user_code","ABCD"},{"verification_uri","https://streamboard.sip1ay.pro/"},{"expires_in",1},{"interval",1}}}; };
        sb::Api api(transport,store,"1.0.0"); QSignalSpy finished(&api,&sb::Api::pairingFinished); api.pair("OBS test");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,3000); QCOMPARE(finished.at(0).at(0).toString(),"expired_token"); QVERIFY(!api.connected()); QCOMPARE(transport.requests.size(),1);
    }
    void mappingSaveContainsOnlySelectedPairAndSurvivesReload() {
        FakeTransport transport; MemoryStore store; store.seed(); QJsonObject saved; int completed = 0;
        transport.handler = [&](const Request &r) -> sb::Response {
            if(r.method == "PUT") { saved = r.body; return {200,saved}; }
            return {200,{{"items",QJsonArray{saved}}}};
        };
        sb::Api api(transport,store,"1.0.0");
        const QJsonObject mapping{{"game",sb::Game{"steam:1","steam","Closed Game","game.exe"}.json()},{"platform_id","vkplaylive"},{"category",QJsonObject{{"id","1"},{"name","Game"},{"type","game"}}}};
        api.request("PUT","mappings",mapping,[&](auto r) { QVERIFY(r.ok()); ++completed; });
        api.request("GET","mappings",{},[&](auto r) { QCOMPARE(sb::mappingRows(r.body["items"].toArray()).size(),1); ++completed; });
        QTRY_COMPARE(completed,2); QCOMPARE(saved,mapping); QCOMPARE(saved.size(),3); QVERIFY(!saved.contains("resolution_source"));
    }
    void unsafeErrorBodyIsNeverReflected() {
        QCOMPARE(sb::safeError(500,{{"detail",QJsonObject{{"code","secret token C:/Private/path"}}}}),"http_500");
        QCOMPARE(sb::safeError(429,{{"detail",QJsonObject{{"code","desktop_rate_limited"}}}}),"desktop_rate_limited");
    }
    void trackerRetryKeepsEventAndDiscardsAfterFocusLoss() {
        FakeTransport transport; MemoryStore store; store.seed(); int attempts = 0;
        transport.handler = [&](const Request &) { ++attempts; return sb::Response{0,{},"network_error"}; };
        sb::Api api(transport,store,"1.0.0"); sb::Tracker tracker(api); const sb::Game game{"steam:1","steam","Game","game.exe"};
        for(qint64 t = 0; t <= 10000; t += 2000) tracker.sample(game,t);
        QTRY_COMPARE(attempts,1); QTest::qWait(5);
        tracker.sample(game,12000); tracker.sample(game,14000); tracker.sample(game,16000);
        QTRY_COMPARE(attempts,2); QCOMPARE(transport.requests[0].body,transport.requests[1].body);
        tracker.sample({},18000); tracker.sample({},20000); tracker.sample({},22000); QTest::qWait(5); QCOMPARE(attempts,2);
    }
    void partialSuccessIsReturnedIndependently() {
        FakeTransport transport; MemoryStore store; store.seed();
        const QJsonArray results{QJsonObject{{"platform_id","twitch"},{"status","updated"}},QJsonObject{{"platform_id","vkplaylive"},{"status","needs_mapping"}}};
        transport.handler = [&](const Request &) { return sb::Response{200,{{"results",results}}}; };
        sb::Api api(transport,store,"1.0.0"); sb::Tracker tracker(api); QSignalSpy returned(&tracker,&sb::Tracker::resultsChanged);
        for(qint64 t = 0; t <= 10000; t += 2000) tracker.sample(sb::Game{"steam:1","steam","Game","game.exe"},t);
        QTRY_COMPARE(returned.count(),1); QCOMPARE(returned[0][0].toJsonArray(),results);
    }
    void queuedStaleGameIsNotSent() {
        FakeTransport transport; MemoryStore store; store.seed(true); bool current = true,completed = false;
        transport.handler = [&](const Request &) { current = false; return sb::Response{200,tokens()}; };
        sb::Api api(transport,store,"1.0.0"); api.request("POST","sync",{{"event_id","obsolete"}},[&](auto r) { QCOMPARE(r.error,"cancelled"); completed = true; },[&] { return current; });
        QTRY_VERIFY(completed); QCOMPARE(transport.requests.size(),1);
    }
    void sleepGapAndPauseRequireFreshStability() {
        FakeTransport transport; MemoryStore store; store.seed(); int requests = 0;
        transport.handler = [&](const Request &) { ++requests; return sb::Response{200,{}}; };
        sb::Api api(transport,store,"1.0.0"); sb::Tracker tracker(api); const sb::Game game{"steam:1","steam","Game","game.exe"};
        tracker.sample(game,0); tracker.sample(game,2000); tracker.sample(game,100000); QTest::qWait(5); QCOMPARE(requests,0);
        for(qint64 t = 102000; t <= 110000; t += 2000) tracker.sample(game,t);
        QTRY_COMPARE(requests,1); tracker.setPaused(true); tracker.sample(game,112000); tracker.setPaused(false); tracker.sample(game,114000);
        QTest::qWait(5); QCOMPARE(requests,1);
    }
};
int runApiTests(int argc,char **argv) { ApiTests tests; return QTest::qExec(&tests,argc,argv); }
#include "api_tests.moc"
