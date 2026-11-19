// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracker.h"
#include <QPointer>
#include <QUuid>

namespace sb {
Tracker::Tracker(Api &a,QObject *parent) : QObject(parent),api(a) {
    connect(&api,&Api::connectionChanged,this,[this](bool) { reset(); });
}
void Tracker::reset() {
    ++revision; state.reset(); pending = {}; retryAt = 0; retrySeconds = 5; lastSample = -1;
}
void Tracker::setPaused(bool value) {
    isPaused = value; reset();
    emit statusChanged(value ? "paused" : "searching",{});
}
void Tracker::accelerate() { state.accelerate(); }
void Tracker::sample(std::optional<Game> game,qint64 timestamp) {
    now = timestamp;
    if(lastSample >= 0 && (now - lastSample > 6000 || now < lastSample)) reset();
    lastSample = now;
    if((!current && game) || (current && !game) || (current && game && current->key != game->key)) reset();
    current = std::move(game);
    if(isPaused) { emit statusChanged("paused",current ? current->name : QString()); return; }
    if(!current) { emit statusChanged("searching",{}); state.observe({},now); return; }
    if(current->needsName) {
        state.observe({},now); emit statusChanged("needs_name",current->name);
        if(lastNotified != current->key) { lastNotified = current->key; emit mappingNeeded(*current); }
        return;
    }
    const auto action = state.observe(current,now);
    if(!api.connected()) { emit statusChanged("not_connected",current->name); return; }
    emit statusChanged(state.stable(now) ? "tracking" : "debouncing",current->name);
    if(inFlight) { if(action) state.accelerate(); return; }
    if(!pending.isEmpty()) { if(now >= retryAt) publish(); return; }
    if(action) {
        pending = {{"event_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
                   {"observed_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                   {"game",action->json()}};
        publish();
    }
}
void Tracker::publish() {
    if(inFlight || pending.isEmpty() || !current || isPaused) return;
    const auto generation = revision;
    const auto key = current->key;
    inFlight = true;
    const QPointer<Tracker> guard(this);
    api.request("POST","sync",pending,[guard,generation](Response r) {
        if(!guard) return;
        auto &self = *guard; self.inFlight = false;
        if(generation != self.revision) return;
        if(r.ok()) {
            self.pending = {}; self.retrySeconds = 5;
            const auto results = r.body["results"].toArray(); emit self.resultsChanged(results);
            for(const auto &v : results) if(v.toObject()["status"] == "needs_mapping" && self.current) {
                if(self.lastNotified != "server:" + self.current->key) {
                    self.lastNotified = "server:" + self.current->key; emit self.mappingNeeded(*self.current);
                }
            }
        } else if(r.error != "cancelled") {
            emit self.statusChanged(r.error,self.current ? self.current->name : QString());
            self.retryAt = self.now + qMax(self.retrySeconds,r.retryAfter)*1000;
            self.retrySeconds = qMin(60,self.retrySeconds*2);
        }
    },[guard,generation,key] {
        return guard && guard->revision == generation && !guard->isPaused && guard->current && guard->current->key == key;
    });
}
}
