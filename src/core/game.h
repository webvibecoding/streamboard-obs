// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QStringList>
#include <QVector>
#include <optional>

namespace sb {
struct Game {
    QString key, source, name, executable;
    bool needsName = false;
    QJsonObject json() const;
    static Game fromJson(const QJsonObject &value);
    bool operator==(const Game &) const = default;
};
struct Process { QString name, path, productName, description; };
struct CatalogEntry { Game game; QString directory, launchExecutable; };
QString steamKey(QString appId);
QString epicKey(QString nameSpace, QString item);
QString executableKey(QString identity);
QString normalizedPath(QString path);
bool excludedExecutable(QString name);
QHash<QString, QString> parseVdf(const QString &text);
QStringList steamLibraries(const QString &text);
std::optional<CatalogEntry> steamManifest(const QString &text, const QString &steamApps);
std::optional<CatalogEntry> epicManifest(const QByteArray &json);
std::optional<Game> resolveGame(const Process &process, QVector<CatalogEntry> catalog,
                              const QHash<QString, QString> &manualNames);
struct MappingRow {
    Game game;
    QJsonObject twitch, vk;
};
QVector<MappingRow> mappingRows(const QJsonArray &mappings);

// All intervals are monotonic milliseconds. Focus loss resets stability immediately.
class DetectionState {
public:
    std::optional<Game> observe(const std::optional<Game> &game, qint64 now);
    void reset();
    void accelerate();
    bool stable(qint64 now) const;
private:
    std::optional<Game> candidate;
    qint64 since = 0, lastSent = -1;
};
}
Q_DECLARE_METATYPE(sb::Game)
