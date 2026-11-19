// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/api.h"
#include "core/game.h"
#include <QFutureWatcher>
#include <QMutex>
#include <QSettings>
#include <QTimer>
#include <memory>

namespace sb {
class WindowsCredentialStore final : public CredentialStore {
public:
    std::optional<Credential> read() override;
    bool write(const Credential &) override;
    bool clear() override;
};
class InstanceLease {
public:
    InstanceLease();
    ~InstanceLease();
    bool owned() const { return handle != nullptr; }
private:
    void *handle = nullptr;
};
Process foregroundProcess();
QVector<CatalogEntry> installedGames();

class Detector final : public QObject {
    Q_OBJECT
public:
    explicit Detector(QString configDirectory,QObject *parent = nullptr);
    ~Detector() override;
    void start();
    void stop();
    void remember(const Game &game);
    void restoreNames(const QJsonArray &mappings);
signals:
    void sampled(std::optional<sb::Game> game);
    void resumed();
private:
    QTimer timer;
    QFutureWatcher<std::optional<Game>> watcher;
    QString configDirectory;
    QHash<QString,QString> names;
    QVector<CatalogEntry> catalog;
    bool running = false;
    qint64 lastCatalogLoad = 0;
    std::shared_ptr<QVector<CatalogEntry>> catalogResult;
    void sample();
    void saveNames();
};
}
Q_DECLARE_METATYPE(std::optional<sb::Game>)
