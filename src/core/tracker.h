// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "api.h"
#include "game.h"
#include <QObject>
#include <QJsonArray>
#include <optional>

namespace sb {
class Tracker final : public QObject {
    Q_OBJECT
public:
    explicit Tracker(Api &api,QObject *parent = nullptr);
    void sample(std::optional<Game> game,qint64 monotonicMs);
    void setPaused(bool value);
    void reset();
    void accelerate();
    bool paused() const { return isPaused; }
    std::optional<Game> currentGame() const { return current; }
signals:
    void statusChanged(QString state,QString gameName);
    void resultsChanged(QJsonArray results);
    void mappingNeeded(sb::Game game);
private:
    Api &api;
    DetectionState state;
    std::optional<Game> current;
    QJsonObject pending;
    qint64 now = 0,lastSample = -1,retryAt = 0;
    int retrySeconds = 5;
    bool isPaused = false,inFlight = false;
    quint64 revision = 0;
    QString lastNotified;
    void publish();
};
}
