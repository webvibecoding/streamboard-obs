// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform.h"
#include <windows.h>
#include <wincred.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

namespace sb {
static constexpr wchar_t credentialTarget[] = L"Streamboard.OBS.DeviceSession.v1";
std::optional<Credential> WindowsCredentialStore::read() {
    PCREDENTIALW value = nullptr;
    if(!CredReadW(credentialTarget,CRED_TYPE_GENERIC,0,&value)) return {};
    const auto data = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(value->CredentialBlob),int(value->CredentialBlobSize))).object();
    CredFree(value);
    Credential result{data["access"].toString(),data["refresh"].toString(),data["device_id"].toString(),
                      QDateTime::fromString(data["expires"].toString(),Qt::ISODateWithMs)};
    if(result.access.isEmpty() || result.refresh.isEmpty() || !result.expires.isValid()) return {};
    return result;
}
bool WindowsCredentialStore::write(const Credential &c) {
    auto bytes = QJsonDocument(QJsonObject{{"access",c.access},{"refresh",c.refresh},{"device_id",c.deviceId},
        {"expires",c.expires.toString(Qt::ISODateWithMs)}}).toJson(QJsonDocument::Compact);
    if(bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
    CREDENTIALW value{};
    value.Type = CRED_TYPE_GENERIC; value.TargetName = const_cast<wchar_t *>(credentialTarget);
    value.Persist = CRED_PERSIST_LOCAL_MACHINE; value.UserName = const_cast<wchar_t *>(L"Streamboard OBS");
    value.CredentialBlobSize = DWORD(bytes.size()); value.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    const bool ok = CredWriteW(&value,0) != FALSE;
    SecureZeroMemory(bytes.data(),size_t(bytes.size())); return ok;
}
bool WindowsCredentialStore::clear() {
    return CredDeleteW(credentialTarget,CRED_TYPE_GENERIC,0) || GetLastError() == ERROR_NOT_FOUND;
}
InstanceLease::InstanceLease() {
    HANDLE token = nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return;
    DWORD size = 0; GetTokenInformation(token,TokenUser,nullptr,0,&size);
    QByteArray user(qsizetype(size),Qt::Uninitialized);
    if(!GetTokenInformation(token,TokenUser,user.data(),size,&size)) { CloseHandle(token); return; }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid,&sid)) return;
    const auto name = QString("Global\\Streamboard.OBS.Session.%1").arg(QString::fromWCharArray(sid));
    LocalFree(sid);
    const auto native = name.toStdWString();
    HANDLE value = CreateMutexW(nullptr,TRUE,native.c_str());
    const auto error = GetLastError();
    if(value && error != ERROR_ALREADY_EXISTS) handle = value;
    else if(value) CloseHandle(value);
}
InstanceLease::~InstanceLease() { if(handle) { ReleaseMutex(handle); CloseHandle(handle); } }
static QString processName(DWORD id) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry); QString result;
    if(Process32FirstW(snapshot,&entry)) do { if(entry.th32ProcessID == id) { result = QString::fromWCharArray(entry.szExeFile); break; } } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot); return result;
}
static QString metadata(const QByteArray &version,const QString &key) {
    struct Translation { WORD language,codepage; };
    Translation *translations = nullptr; UINT size = 0;
    if(!VerQueryValueW(version.constData(),L"\\VarFileInfo\\Translation",reinterpret_cast<void **>(&translations),&size)) return {};
    for(UINT i = 0; i < size / sizeof(Translation); ++i) {
        const auto query = QString("\\StringFileInfo\\%1%2\\%3").arg(translations[i].language,4,16,QChar('0')).arg(translations[i].codepage,4,16,QChar('0')).arg(key).toStdWString();
        wchar_t *text = nullptr; UINT length = 0;
        if(VerQueryValueW(version.constData(),query.c_str(),reinterpret_cast<void **>(&text),&length) && length > 1)
            return QString::fromWCharArray(text,int(length)-1);
    }
    return {};
}
Process foregroundProcess() {
    const auto window = GetForegroundWindow(); DWORD id = 0;
    if(!window || !GetWindowThreadProcessId(window,&id) || !id || id == GetCurrentProcessId()) return {};
    Process result;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);
    if(process) {
        wchar_t path[32768]; DWORD length = 32768;
        if(QueryFullProcessImageNameW(process,0,path,&length)) result.path = QString::fromWCharArray(path,int(length));
        CloseHandle(process);
    }
    result.name = result.path.isEmpty() ? processName(id) : QString(result.path).replace('\\','/').section('/',-1);
    if(excludedExecutable(result.name)) return {};
    if(!result.path.isEmpty()) {
        const auto native = result.path.toStdWString(); DWORD unused = 0;
        const auto size = GetFileVersionInfoSizeW(native.c_str(),&unused);
        if(size && size <= 4*1024*1024) {
            QByteArray version(qsizetype(size),Qt::Uninitialized);
            if(GetFileVersionInfoW(native.c_str(),0,size,version.data())) {
                result.productName = metadata(version,"ProductName"); result.description = metadata(version,"FileDescription");
            }
        }
    }
    return result;
}
static QByteArray readSmallFile(const QString &path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size() > 4*1024*1024) return {};
    return file.readAll();
}
QVector<CatalogEntry> installedGames() {
    QSettings steam("HKEY_CURRENT_USER\\Software\\Valve\\Steam",QSettings::NativeFormat);
    QSettings machine("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",QSettings::NativeFormat);
    QStringList roots{steam.value("SteamPath").toString(),machine.value("InstallPath").toString(),qEnvironmentVariable("ProgramFiles(x86)")+"/Steam"};
    QStringList libraries;
    for(auto root : roots) {
        if(root.isEmpty()) continue;
        root.replace('\\','/'); libraries.append(root);
        libraries.append(steamLibraries(QString::fromUtf8(readSmallFile(QDir(root).filePath("steamapps/libraryfolders.vdf")))));
    }
    libraries.removeDuplicates(); QVector<CatalogEntry> result; QSet<QString> keys;
    for(auto root : libraries) {
        const QDir steamApps(QDir(root).filePath("steamapps"));
        for(const auto &file : steamApps.entryList({"appmanifest_*.acf"},QDir::Files)) {
            auto entry = steamManifest(QString::fromUtf8(readSmallFile(steamApps.filePath(file))),steamApps.path());
            if(entry && !keys.contains(entry->game.key)) { keys.insert(entry->game.key); result.append(*entry); }
        }
    }
    const QDir epic(qEnvironmentVariable("ProgramData","C:/ProgramData")+"/Epic/EpicGamesLauncher/Data/Manifests");
    for(const auto &file : epic.entryList({"*.item"},QDir::Files)) {
        auto entry = epicManifest(readSmallFile(epic.filePath(file)));
        if(entry && !keys.contains(entry->game.key)) { keys.insert(entry->game.key); result.append(*entry); }
    }
    return result;
}
Detector::Detector(QString directory,QObject *parent) : QObject(parent),timer(this),watcher(this),configDirectory(std::move(directory)) {
    const auto stored = QJsonDocument::fromJson(readSmallFile(QDir(configDirectory).filePath("executable-names.json"))).object();
    for(auto it = stored.begin(); it != stored.end(); ++it) if(it.key().startsWith("exe:") && it.value().isString()) names.insert(it.key(),it.value().toString());
    timer.setInterval(2000);
    connect(&timer,&QTimer::timeout,this,&Detector::sample);
    connect(&watcher,&QFutureWatcher<std::optional<Game>>::finished,this,[this] {
        if(catalogResult) { catalog = *catalogResult; catalogResult.reset(); }
        if(running) emit sampled(watcher.result());
    });
}
Detector::~Detector() { stop(); }
void Detector::start() { running = true; timer.start(); sample(); }
void Detector::stop() { running = false; timer.stop(); watcher.waitForFinished(); }
void Detector::sample() {
    if(!running || watcher.isRunning()) return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const bool reload = !lastCatalogLoad || now - lastCatalogLoad > 300000 || now < lastCatalogLoad;
    if(reload) { lastCatalogLoad = now; catalogResult = std::make_shared<QVector<CatalogEntry>>(); }
    const auto result = catalogResult; const auto existing = catalog; const auto manual = names;
    watcher.setFuture(QtConcurrent::run([result,existing,manual] {
        auto games = existing;
        if(result) { games = installedGames(); *result = games; }
        return resolveGame(foregroundProcess(),std::move(games),manual);
    }));
}
void Detector::saveNames() {
    QDir().mkpath(configDirectory); QJsonObject data;
    for(auto it = names.cbegin(); it != names.cend(); ++it) data.insert(it.key(),it.value());
    QSaveFile file(QDir(configDirectory).filePath("executable-names.json"));
    if(file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(data).toJson()); file.commit(); }
}
void Detector::remember(const Game &game) {
    if(game.source == "exe" && !game.name.trimmed().isEmpty()) { names.insert(game.key,game.name); saveNames(); }
}
void Detector::restoreNames(const QJsonArray &mappings) {
    for(const auto &v : mappings) {
        const auto game = Game::fromJson(v.toObject()["game"].toObject());
        if(game.source == "exe" && game.key.startsWith("exe:") && !game.name.isEmpty()) names.insert(game.key,game.name);
    }
    saveNames();
}
}
