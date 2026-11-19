// SPDX-License-Identifier: GPL-2.0-or-later
#include "api.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QSslSocket>

namespace sb {
QString safeError(int status, const QJsonObject &body) {
    auto code = body["detail"].toObject()["code"].toString();
    if(code.isEmpty()) code = body["error"].toString();
    static const QRegularExpression allowed("^(?:(?:desktop|twitch|vkplaylive)_[a-zA-Z0-9_]{1,65}|authorization_pending|slow_down|access_denied|expired_token|invalid_grant)$");
    return allowed.match(code).hasMatch() ? code : QString("http_%1").arg(status);
}
NetworkTransport::NetworkTransport(QObject *parent) : Transport(parent),network(this) {}
QString networkErrorCode(QNetworkReply::NetworkError error) {
    switch(error) {
    case QNetworkReply::HostNotFoundError: return "network_dns_error";
    case QNetworkReply::ConnectionRefusedError: return "network_connection_refused";
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: return "network_timeout";
    case QNetworkReply::SslHandshakeFailedError: return "network_tls_error";
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
    case QNetworkReply::UnknownProxyError: return "network_proxy_error";
    default: return "network_error";
    }
}
void NetworkTransport::send(const QByteArray &method,const QUrl &url,const Headers &headers,const QJsonObject &body,Reply done) {
    if(url.scheme() == "https" && !QSslSocket::supportsSsl()) {
        qWarning("[Streamboard OBS] transport failure: network_tls_unavailable");
        QTimer::singleShot(0,this,[done = std::move(done)]() mutable { done({0,{},"network_tls_unavailable"}); });
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept","application/json");
    for(auto it = headers.cbegin(); it != headers.cend(); ++it) request.setRawHeader(it.key(),it.value());
    QByteArray data;
    if(!body.isEmpty()) { data = QJsonDocument(body).toJson(QJsonDocument::Compact); request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json"); }
    auto *reply = network.sendCustomRequest(request,method,data);
    reply->setReadBufferSize(2*1024*1024 + 1);
    connect(reply,&QIODevice::readyRead,reply,[reply] { if(reply->bytesAvailable() > 2*1024*1024) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[reply,done = std::move(done)]() mutable {
        Response result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto data = reply->read(2*1024*1024 + 1);
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(data,&parse);
        result.body = document.object();
        const auto retry = reply->rawHeader("Retry-After");
        bool numeric = false; result.retryAfter = retry.toInt(&numeric);
        if(!numeric && !retry.isEmpty()) result.retryAfter = int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(retry),Qt::RFC2822Date)));
        result.retryAfter = qBound(0,result.retryAfter,3600);
        if(data.size() > 2*1024*1024) result.error = "response_too_large";
        else if(!result.status || (result.status >= 200 && result.status < 300 && reply->error() != QNetworkReply::NoError)) result.error = networkErrorCode(reply->error());
        else if(result.status < 200 || result.status >= 300) result.error = safeError(result.status,result.body);
        else if(result.status != 204 && (parse.error != QJsonParseError::NoError || !document.isObject())) result.error = "invalid_server_response";
        if(result.error.startsWith("network_")) qWarning("[Streamboard OBS] transport failure: %s (Qt code %d)",qPrintable(result.error),int(reply->error()));
        reply->deleteLater(); done(std::move(result));
    });
}
void NetworkTransport::cancelAll() {
    const auto replies = network.findChildren<QNetworkReply *>();
    for(auto *reply : replies) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
}
QString pkceVerifier() {
    QByteArray bytes(32,Qt::Uninitialized);
    for(auto &byte : bytes) byte = char(QRandomGenerator::system()->generate() & 255);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QString pkceChallenge(const QString &verifier) {
    return QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(),QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
bool trustedPairingUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == "https" && url.host() == "streamboard.sip1ay.pro"
        && url.port(443) == 443 && url.userInfo().isEmpty();
}
QUrl Api::productionUrl() { return QUrl("https://streamboard.sip1ay.pro/api/desktop/v1/"); }
Api::Api(Transport &t,CredentialStore &s,QString v,QObject *parent,QUrl b)
    : QObject(parent),transport(t),store(s),version(std::move(v)),base(std::move(b)),credential(store.read()) {}
void Api::raw(QByteArray method,QString route,QJsonObject body,QString token,Reply done) {
    Headers headers{{"User-Agent",("Streamboard-OBS/" + version).toUtf8()}};
    if(!token.isEmpty()) headers.insert("Authorization","Bearer " + token.toUtf8());
    const QPointer<Api> guard(this);
    transport.send(method,base.resolved(QUrl(route)),headers,body,[guard,done = std::move(done)](Response response) mutable {
        if(guard && !guard->stopped) done(std::move(response));
    });
}
void Api::request(QByteArray method,QString route,QJsonObject body,Reply reply,std::function<bool()> current) {
    if(stopped) { reply({0,{},"cancelled"}); return; }
    queue.enqueue({std::move(method),std::move(route),std::move(body),std::move(reply),std::move(current)}); pump();
}
void Api::pump() {
    if(stopped || busy || pairing || queue.isEmpty()) return;
    busy = true; authorize(queue.dequeue());
}
void Api::authorize(Job job,bool retried) {
    if(job.current && !job.current()) { finish(std::move(job),{0,{},"cancelled"}); return; }
    if(!credential) { finish(std::move(job),{401,{},"desktop_pairing_required"}); return; }
    if(!retried && credential->expires > QDateTime::currentDateTimeUtc().addSecs(60)) { deliver(std::move(job),false); return; }
    raw("POST","token",{{"grant_type","refresh_token"},{"refresh_token",credential->refresh}},{},
        [this,job = std::move(job)](Response response) mutable {
            if(!response.ok()) {
                if(response.status == 400 || response.status == 401) forget();
                finish(std::move(job),std::move(response)); return;
            }
            if(!saveToken(response.body)) { finish(std::move(job),{0,{},"credential_store_failed"}); return; }
            deliver(std::move(job),true);
        });
}
void Api::deliver(Job job,bool retried) {
    if(job.current && !job.current()) { finish(std::move(job),{0,{},"cancelled"}); return; }
    // Evaluate request arguments before moving job into the callback (argument order is unspecified).
    const auto method = job.method; const auto route = job.route; const auto body = job.body;
    raw(method,route,body,credential->access,[this,job = std::move(job),retried](Response response) mutable {
        if(response.status == 401 && response.error.startsWith("desktop_") && !retried) {
            if(response.error == "desktop_device_revoked") { forget(); finish(std::move(job),std::move(response)); }
            else authorize(std::move(job),true);
            return;
        }
        if(response.status == 401 && response.error.startsWith("desktop_") && retried) forget();
        finish(std::move(job),std::move(response));
    });
}
void Api::finish(Job job,Response response) {
    busy = false;
    const QPointer<Api> guard(this);
    job.reply(std::move(response));
    if(guard) QTimer::singleShot(0,this,[this] { pump(); });
}
bool Api::saveToken(const QJsonObject &body) {
    const bool wasConnected = credential.has_value();
    const auto access = body["access_token"].toString(), refresh = body["refresh_token"].toString();
    const int expires = body["expires_in"].toInt();
    if(access.isEmpty() || refresh.isEmpty() || expires <= 0) { forget(); return false; }
    Credential next{access,refresh,body["device_id"].toString(),QDateTime::currentDateTimeUtc().addSecs(expires)};
    if(!store.write(next)) { forget(); return false; }
    credential = std::move(next); if(!wasConnected) emit connectionChanged(true); return true;
}
void Api::forget() { credential.reset(); store.clear(); emit connectionChanged(false); }
void Api::pair(const QString &deviceName) {
    if(stopped || pairing || busy || credential) return;
    pairing = true; const auto generation = ++pairingGeneration;
    verifier = pkceVerifier();
    raw("POST","pairings",{{"device_name",deviceName.left(80)},{"code_challenge",pkceChallenge(verifier)},
        {"platform","windows"},{"app_version",version}},{},[this,generation](Response r) {
        if(generation != pairingGeneration) return;
        if(!r.ok()) { endPairing(r.error); return; }
        // value() does not insert an absent optional field or invalidate a QJsonValueRef.
        const auto url = QUrl(r.body.value("verification_uri_complete").toString(r.body.value("verification_uri").toString()));
        if(!trustedPairingUrl(url) || r.body.value("device_code").toString().isEmpty()) { endPairing("invalid_pairing_response"); return; }
        deviceCode = r.body["device_code"].toString(); pollInterval = qBound(1,r.body["interval"].toInt(5),30);
        pairingDeadline = QDateTime::currentDateTimeUtc().addSecs(qBound(1,r.body["expires_in"].toInt(600),600));
        emit pairingStarted(r.body["user_code"].toString(),url);
        QTimer::singleShot(pollInterval*1000,this,[this,generation] { pollPairing(generation); });
    });
}
void Api::pollPairing(quint64 generation) {
    if(stopped || !pairing || generation != pairingGeneration) return;
    if(QDateTime::currentDateTimeUtc() >= pairingDeadline) { endPairing("expired_token"); return; }
    raw("POST","token",{{"grant_type","device_code"},{"device_code",deviceCode},{"code_verifier",verifier}},{},
        [this,generation](Response r) {
            if(generation != pairingGeneration) return;
            const auto error = r.body["error"].toString();
            if(r.ok() && !r.body["access_token"].toString().isEmpty()) {
                const bool saved = saveToken(r.body); endPairing(saved ? QString() : "credential_store_failed"); return;
            }
            if(error == "slow_down" || r.status == 429) pollInterval = qMin(60,qMax(pollInterval+5,r.retryAfter));
            else if(error != "authorization_pending" && r.status != 0 && r.status < 500) { endPairing(r.error.isEmpty() ? "invalid_pairing_response" : r.error); return; }
            QTimer::singleShot(pollInterval*1000,this,[this,generation] { pollPairing(generation); });
        });
}
void Api::endPairing(const QString &error) {
    pairing = false; verifier.clear(); deviceCode.clear(); emit pairingFinished(error); pump();
}
void Api::disconnectDevice(Reply done) {
    ++pairingGeneration; pairing = false; verifier.clear(); deviceCode.clear();
    request("DELETE","session",{},[this,done = std::move(done)](Response r) mutable { forget(); done(std::move(r)); });
}
void Api::stop() {
    stopped = true; ++pairingGeneration; queue.clear(); verifier.clear(); deviceCode.clear();
    transport.cancelAll(); credential.reset();
}
}
