// SPDX-License-Identifier: GPL-2.0-or-later
#include "dock.h"
#include "ui.h"
#include "version.h"
#include <windows.h>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QVersionNumber>

namespace sb {
static QLabel *plainLabel(const QString &value,QWidget *parent) {
    auto *label = new QLabel(value,parent); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
static void setupTable(QTableWidget *table,const QStringList &headers) {
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide(); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setWordWrap(true);
}
static QString resultName(const QString &status) {
    static const QHash<QString,const char *> names{{"updated","Updated"},{"unchanged","Unchanged"},{"offline","Offline"},
        {"not_connected","PlatformDisconnected"},{"needs_mapping","NeedsMapping"},{"unsupported","Unsupported"},{"error","ResultError"}};
    return text(names.value(status,"ResultError"));
}
Dock::Dock(QString directory,QWidget *parent) : QWidget(parent),settings(QDir(directory).filePath("settings.ini"),QSettings::IniFormat) {
    setWindowIcon(QIcon(":/streamboard.ico")); setMinimumSize(310,260);
    auto *layout = new QVBoxLayout(this);
    if(!lease.owned()) { layout->addWidget(plainLabel(text("OtherInstance"),this)); return; }
    api = std::make_unique<Api>(transport,credentialStore,STREAMBOARD_VERSION,this);
    detector = std::make_unique<Detector>(directory,this); tracker = std::make_unique<Tracker>(*api,this);
    auto *heading = new QHBoxLayout;
    auto *icon = new QLabel(this); icon->setPixmap(QIcon(":/streamboard.ico").pixmap(28,28)); heading->addWidget(icon);
    heading->addWidget(plainLabel("Streamboard OBS " STREAMBOARD_VERSION,this)); heading->addStretch(); layout->addLayout(heading);
    tabs = new QTabWidget(this); layout->addWidget(tabs);
    auto *statusPage = new QWidget(tabs); auto *statusLayout = new QVBoxLayout(statusPage);
    connection = plainLabel({},statusPage); statusLayout->addWidget(connection);
    auto *connectRow = new QHBoxLayout;
    connectButton = new QPushButton(text("Connect"),statusPage); disconnectButton = new QPushButton(text("Disconnect"),statusPage);
    connectRow->addWidget(connectButton); connectRow->addWidget(disconnectButton); statusLayout->addLayout(connectRow);
    game = plainLabel(text("NoGame"),statusPage); status = plainLabel(text("Searching"),statusPage);
    statusLayout->addWidget(game); statusLayout->addWidget(status);
    results = new QTableWidget(2,3,statusPage); setupTable(results,{text("Platform"),text("Status"),text("Category")});
    for(int row = 0; row < 2; ++row) {
        results->setItem(row,0,new QTableWidgetItem(row ? "VK Play Live" : "Twitch"));
        results->setItem(row,1,new QTableWidgetItem(text("UnknownStatus"))); results->setItem(row,2,new QTableWidgetItem(text("NotSelected")));
    }
    statusLayout->addWidget(results);
    mappingNotice = plainLabel({},statusPage); mappingNotice->hide(); statusLayout->addWidget(mappingNotice);
    auto *controls = new QHBoxLayout;
    pauseButton = new QPushButton(text("Pause"),statusPage); editButton = new QPushButton(text("ChooseCategory"),statusPage);
    editButton->setEnabled(false); controls->addWidget(pauseButton); controls->addWidget(editButton); statusLayout->addLayout(controls);
    auto *mappingPage = new QWidget(tabs); auto *mappingLayout = new QVBoxLayout(mappingPage);
    reloadButton = new QPushButton(text("Reload"),mappingPage); mappingLayout->addWidget(reloadButton);
    mappings = new QTableWidget(mappingPage); setupTable(mappings,{text("GameExe"),"Twitch","VK Play Live"}); mappingLayout->addWidget(mappings);
    mappingMessage = plainLabel(text("EmptyMappings"),mappingPage); mappingLayout->addWidget(mappingMessage);
    mappingLayout->addWidget(plainLabel(text("MappingHint"),mappingPage));
    tabs->addTab(statusPage,text("StateTab")); tabs->addTab(mappingPage,text("MappingsTab"));
    auto *updateButton = new QPushButton(text("CheckUpdates"),this); layout->addWidget(updateButton);
    updateMessage = new QLabel(this); updateMessage->setWordWrap(true); updateMessage->setOpenExternalLinks(true); layout->addWidget(updateMessage);
    connect(connectButton,&QPushButton::clicked,this,[this] {
        if(!api->isPairing()) { connectButton->setEnabled(false); api->pair("OBS — " + qEnvironmentVariable("COMPUTERNAME","Windows")); }
    });
    connect(disconnectButton,&QPushButton::clicked,this,[this] {
        tracker->reset(); tracker->setPaused(true); disconnectButton->setEnabled(false);
        pauseButton->setText(text("Resume"));
        api->disconnectDevice([this](Response r) { if(!r.ok() && r.error != "desktop_pairing_required") connection->setText(text("Disconnected") + " · " + errorText(r.error)); });
    });
    connect(api.get(),&Api::pairingStarted,this,[this](const QString &code,const QUrl &url) {
        connection->setText(text("PairingCode") + " " + code); disconnectButton->setEnabled(true); QDesktopServices::openUrl(url);
    });
    connect(api.get(),&Api::pairingFinished,this,[this](const QString &error) {
        connectButton->setEnabled(!api->connected()); disconnectButton->setEnabled(api->connected());
        if(!error.isEmpty()) connection->setText(errorText(error));
    });
    connect(api.get(),&Api::connectionChanged,this,&Dock::updateConnection);
    connect(detector.get(),&Detector::sampled,this,[this](std::optional<Game> sample) {
        tracker->sample(std::move(sample),clock.elapsed());
    });
    connect(tracker.get(),&Tracker::statusChanged,this,[this](const QString &state,const QString &name) {
        game->setText(name.isEmpty() ? text("NoGame") : name);
        static const QHash<QString,const char *> labels{{"paused","Paused"},{"searching","Searching"},{"not_connected","ConnectRequired"},
            {"needs_name","NameRequired"},{"tracking","Tracking"},{"debouncing","Debouncing"}};
        status->setText(labels.contains(state) ? text(labels.value(state)) : errorText(state));
        editButton->setEnabled(api->connected() && tracker->currentGame().has_value());
    });
    connect(tracker.get(),&Tracker::resultsChanged,this,[this](const QJsonArray &items) {
        bool needsMapping = false;
        for(const auto &v : items) {
            const auto item = v.toObject(); const auto id = item["platform_id"].toString();
            if(id != "twitch" && id != "vkplaylive") continue;
            const int row = id == "twitch" ? 0 : 1;
            results->item(row,1)->setText(resultName(item["status"].toString()));
            results->item(row,2)->setText(item["category"].toObject()["name"].toString(text("NotSelected")));
            if(item["status"] == "error") results->item(row,1)->setToolTip(errorText(safeError(500,{{"error",item["error"]}})));
            else results->item(row,1)->setToolTip({});
            needsMapping |= item["status"] == "needs_mapping";
        }
        mappingNotice->setVisible(needsMapping); if(needsMapping) mappingNotice->setText(text("MappingNotification"));
    });
    connect(tracker.get(),&Tracker::mappingNeeded,this,[this](const Game &) { mappingNotice->setText(text("MappingNotification")); mappingNotice->show(); });
    connect(pauseButton,&QPushButton::clicked,this,[this] {
        tracker->setPaused(!tracker->paused()); settings.setValue("paused",tracker->paused());
        pauseButton->setText(text(tracker->paused() ? "Resume" : "Pause"));
    });
    connect(editButton,&QPushButton::clicked,this,[this] { if(auto selected = tracker->currentGame()) edit(*selected); });
    connect(results,&QTableWidget::cellClicked,this,[this](int row,int) { if(auto selected = tracker->currentGame()) edit(*selected,row == 0 ? "twitch" : "vkplaylive"); });
    connect(reloadButton,&QPushButton::clicked,this,&Dock::loadMappings);
    connect(tabs,&QTabWidget::currentChanged,this,[this](int index) { if(index == 1) loadMappings(); });
    connect(mappings,&QTableWidget::cellClicked,this,[this](int row,int column) {
        if(column > 0 && row >= 0 && row < rows.size()) edit(rows[row].game,column == 1 ? "twitch" : "vkplaylive");
    });
    connect(updateButton,&QPushButton::clicked,this,[this] { checkUpdate(true); });
    clock.start(); updateConnection(api->connected());
    tracker->setPaused(settings.value("paused",false).toBool()); pauseButton->setText(text(tracker->paused() ? "Resume" : "Pause"));
    QCoreApplication::instance()->installNativeEventFilter(this);
    if(!qEnvironmentVariableIsSet("STREAMBOARD_OBS_CI_SMOKE")) {
        detector->start(); QTimer::singleShot(10000,this,[this] { checkUpdate(false); });
    }
}
Dock::~Dock() { shutdown(); }
void Dock::shutdown() {
    if(stopped) return;
    stopped = true; ++sessionRevision;
    if(QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    if(editor) { editor->disconnect(this); editor->close(); }
    if(tracker) tracker->setPaused(true);
    if(api) api->stop();
    transport.cancelAll(); if(detector) detector->stop(); settings.sync();
}
bool Dock::nativeEventFilter(const QByteArray &,void *message,qintptr *) {
    const auto *native = static_cast<MSG *>(message);
    if(tracker && native->message == WM_POWERBROADCAST && (native->wParam == PBT_APMSUSPEND
        || native->wParam == PBT_APMRESUMEAUTOMATIC || native->wParam == PBT_APMRESUMESUSPEND)) tracker->reset();
    return false;
}
void Dock::streamStarted() { if(tracker) tracker->accelerate(); }
bool Dock::smokeCheck() const { return !windowIcon().isNull() && tabs && tabs->count() == 2 && mappings && mappings->columnCount() == 3 && results && results->rowCount() == 2 && api && !api->connected(); }
void Dock::updateConnection(bool connected) {
    ++sessionRevision;
    connectButton->setEnabled(!connected && !api->isPairing()); disconnectButton->setEnabled(connected || api->isPairing());
    connection->setText(text(connected ? "Connected" : "Disconnected"));
    editButton->setEnabled(connected && tracker->currentGame().has_value());
    if(connected) {
        tracker->setPaused(settings.value("paused",false).toBool()); pauseButton->setText(text(tracker->paused() ? "Resume" : "Pause"));
        bootstrap(); loadMappings();
    }
    else {
        savedMappings = {}; rows.clear(); renderMappings(); mappingMessage->setText(text("ConnectRequired"));
        for(int row = 0; row < 2; ++row) { results->item(row,1)->setText(text("UnknownStatus")); results->item(row,2)->setText(text("NotSelected")); }
    }
}
void Dock::bootstrap() {
    const auto revision = sessionRevision;
    api->request("GET","bootstrap",{},[this,revision](Response r) {
        if(stopped || revision != sessionRevision) return;
        if(!r.ok()) { connection->setText(errorText(r.error)); return; }
        connection->setText(text("Connected") + " · " + r.body["device"].toObject()["name"].toString());
        for(const auto &v : r.body["platforms"].toArray()) {
            const auto item = v.toObject(); const auto id = item["id"].toString(); if(id != "twitch" && id != "vkplaylive") continue;
            results->item(id == "twitch" ? 0 : 1,1)->setText(!item["connected"].toBool() ? text("PlatformDisconnected") : item["live"].toBool() ? text("Live") : text("Offline"));
        }
    });
}
void Dock::loadMappings() {
    if(loadingMappings || stopped) return;
    if(!api->connected()) { mappingMessage->setText(text("ConnectRequired")); return; }
    const auto revision = sessionRevision;
    loadingMappings = true; reloadButton->setEnabled(false); mappingMessage->setText(text("Loading"));
    api->request("GET","mappings",{},[this,revision](Response response) {
        loadingMappings = false; reloadButton->setEnabled(true);
        if(stopped || revision != sessionRevision) return;
        if(!response.ok()) { mappingMessage->setText(errorText(response.error)); return; }
        savedMappings = response.body["items"].toArray(); detector->restoreNames(savedMappings);
        rows = mappingRows(savedMappings); renderMappings();
    });
}
void Dock::renderMappings() {
    mappings->setRowCount(int(rows.size()));
    for(int row = 0; row < rows.size(); ++row) {
        const auto &value = rows[row];
        auto *name = new QTableWidgetItem(value.game.name); name->setToolTip(value.game.key + "\n" + value.game.executable); mappings->setItem(row,0,name);
        mappings->setItem(row,1,new QTableWidgetItem(value.twitch["name"].toString(text("NotSelected"))));
        mappings->setItem(row,2,new QTableWidgetItem(value.vk["name"].toString(text("NotSelected"))));
    }
    mappings->resizeRowsToContents(); mappingMessage->setText(rows.isEmpty() ? text("EmptyMappings") : text("MappingCount").arg(rows.size()));
}
void Dock::edit(Game selected,QString platform) {
    if(!api->connected()) { connection->setText(text("ConnectRequired")); return; }
    if(editor) { editor->raise(); editor->activateWindow(); return; }
    editor = new Editor(*api,std::move(selected),std::move(platform),this);
    connect(editor,&Editor::saved,this,&Dock::applyMapping); editor->show();
}
void Dock::applyMapping(Game selected,const QJsonObject &mapping) {
    const auto platform = mapping["platform_id"].toString();
    for(qsizetype i = savedMappings.size(); i > 0; --i) {
        const auto previous = savedMappings[i-1].toObject();
        if(previous["game"].toObject()["key"] == selected.key && previous["platform_id"] == platform) savedMappings.removeAt(i-1);
    }
    savedMappings.append(mapping); rows = mappingRows(savedMappings); renderMappings();
    detector->remember(selected); tracker->reset(); mappingNotice->hide();
}
void Dock::checkUpdate(bool manual) {
    if(stopped || checkingUpdate) return;
    const auto last = settings.value("last_update_check").toDateTime();
    if(!manual && last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 86400) return;
    checkingUpdate = true; settings.setValue("last_update_check",QDateTime::currentDateTimeUtc());
    if(manual) updateMessage->setText(text("CheckingUpdates"));
    transport.send("GET",QUrl("https://api.github.com/repos/webvibecoding/streamboard-obs/releases/latest"),
        {{"User-Agent","Streamboard-OBS/" STREAMBOARD_VERSION}}, {},[this,manual](Response r) {
            if(stopped) return;
            checkingUpdate = false;
            if(!r.ok()) { if(manual) updateMessage->setText(text("UpdateCheckFailed")); return; }
            auto tag = r.body["tag_name"].toString();
            if(!QRegularExpression("^v[0-9]+\\.[0-9]+\\.[0-9]+$").match(tag).hasMatch()) { if(manual) updateMessage->setText(text("UpdateCheckFailed")); return; }
            if(QVersionNumber::fromString(tag.mid(1)) > QVersionNumber::fromString(STREAMBOARD_VERSION)) {
                updateMessage->setText(text("UpdateAvailable").arg(tag.toHtmlEscaped()) + " <a href=\"https://github.com/webvibecoding/streamboard-obs/releases/tag/" + tag + "\">" + text("OpenRelease") + "</a>");
            } else if(manual) updateMessage->setText(text("UpToDate"));
        });
}
}
