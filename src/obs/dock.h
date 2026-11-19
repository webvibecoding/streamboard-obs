// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/tracker.h"
#include "windows/platform.h"
#include "editor.h"
#include <QElapsedTimer>
#include <QAbstractNativeEventFilter>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace sb {
class Dock final : public QWidget, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit Dock(QString configDirectory,QWidget *parent = nullptr);
    ~Dock() override;
    void shutdown();
    void streamStarted();
    bool smokeCheck() const;
protected:
    bool nativeEventFilter(const QByteArray &,void *,qintptr *) override;
private:
    InstanceLease lease;
    WindowsCredentialStore credentialStore;
    NetworkTransport transport;
    std::unique_ptr<Api> api;
    std::unique_ptr<Detector> detector;
    std::unique_ptr<Tracker> tracker;
    QElapsedTimer clock;
    QSettings settings;
    QTabWidget *tabs = nullptr;
    QLabel *connection = nullptr,*status = nullptr,*game = nullptr,*mappingNotice = nullptr,*mappingMessage = nullptr,*updateMessage = nullptr;
    QPushButton *connectButton = nullptr,*disconnectButton = nullptr,*pauseButton = nullptr,*editButton = nullptr,*reloadButton = nullptr;
    QTableWidget *results = nullptr,*mappings = nullptr;
    QPointer<Editor> editor;
    QVector<MappingRow> rows;
    QJsonArray savedMappings;
    bool stopped = false,loadingMappings = false,checkingUpdate = false;
    quint64 sessionRevision = 0;
    void updateConnection(bool connected);
    void bootstrap();
    void loadMappings();
    void renderMappings();
    void edit(Game selected,QString platform = {});
    void applyMapping(Game selected,const QJsonObject &mapping);
    void checkUpdate(bool manual);
};
}
