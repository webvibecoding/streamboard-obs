// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QUrl>
#include <functional>
#include <optional>

namespace sb {
struct Response {
    int status = 0;
    QJsonObject body;
    QString error;
    int retryAfter = 0;
    bool ok() const { return status >= 200 && status < 300 && error.isEmpty(); }
};
QString safeError(int status, const QJsonObject &body);
QString networkErrorCode(QNetworkReply::NetworkError error);
using Reply = std::function<void(Response)>;
using Headers = QHash<QByteArray,QByteArray>;
class Transport : public QObject {
public:
    using QObject::QObject;
    virtual void send(const QByteArray &method, const QUrl &url, const Headers &headers,
                      const QJsonObject &body, Reply done) = 0;
    virtual void cancelAll() = 0;
};
class NetworkTransport final : public Transport {
public:
    explicit NetworkTransport(QObject *parent = nullptr);
    void send(const QByteArray &,const QUrl &,const Headers &,const QJsonObject &,Reply) override;
    void cancelAll() override;
private:
    QNetworkAccessManager network;
};
struct Credential {
    QString access, refresh, deviceId;
    QDateTime expires;
};
class CredentialStore {
public:
    virtual ~CredentialStore() = default;
    virtual std::optional<Credential> read() = 0;
    virtual bool write(const Credential &) = 0;
    virtual bool clear() = 0;
};
QString pkceVerifier();
QString pkceChallenge(const QString &verifier);
bool trustedPairingUrl(const QUrl &url);

class Api final : public QObject {
    Q_OBJECT
public:
    static QUrl productionUrl();
    Api(Transport &transport, CredentialStore &store, QString version, QObject *parent = nullptr,
        QUrl base = productionUrl());
    bool connected() const { return credential.has_value(); }
    bool isPairing() const { return pairing; }
    void request(QByteArray method, QString route, QJsonObject body, Reply reply,
                 std::function<bool()> current = {});
    void pair(const QString &deviceName);
    void disconnectDevice(Reply done);
    void stop();
signals:
    void connectionChanged(bool connected);
    void pairingStarted(QString userCode, QUrl url);
    void pairingFinished(QString error);
private:
    struct Job { QByteArray method; QString route; QJsonObject body; Reply reply; std::function<bool()> current; };
    Transport &transport;
    CredentialStore &store;
    QString version;
    QUrl base;
    std::optional<Credential> credential;
    QQueue<Job> queue;
    bool busy = false, stopped = false, pairing = false;
    quint64 pairingGeneration = 0;
    QString verifier, deviceCode;
    QDateTime pairingDeadline;
    int pollInterval = 5;
    void pump();
    void authorize(Job job, bool retried = false);
    void deliver(Job job, bool retried);
    void finish(Job job, Response response);
    void raw(QByteArray method, QString route, QJsonObject body, QString token, Reply done);
    bool saveToken(const QJsonObject &body);
    void forget();
    void pollPairing(quint64 generation);
    void endPairing(const QString &error);
};
}
