// SPDX-License-Identifier: GPL-2.0-or-later
#include "game.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace sb {
// QString's full case mapping expands U+0130; .NET invariant casing preserves it.
// Use scalar/simple mappings and the invariant Turkish-I exception for key parity.
static QString invariantLower(const QString &value) {
    QList<char32_t> scalars;
    for(const char32_t scalar : value.toUcs4()) scalars.append(scalar == 0x0130 ? scalar : QChar::toLower(scalar));
    return QString::fromUcs4(scalars.constData(),scalars.size());
}
QJsonObject Game::json() const {
    return {{"key",key},{"source",source},{"display_name",name},{"executable_name",executable}};
}
Game Game::fromJson(const QJsonObject &v) {
    return {v["key"].toString(),v["source"].toString(),v["display_name"].toString(),v["executable_name"].toString()};
}
QString steamKey(QString appId) {
    appId.remove(QRegularExpression("[^0-9]"));
    return appId.isEmpty() ? QString() : "steam:" + appId;
}
QString epicKey(QString nameSpace, QString item) {
    auto part = [](const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(invariantLower(s.trimmed()))); };
    return nameSpace.trimmed().isEmpty() || item.trimmed().isEmpty() ? QString() : "epic:" + part(nameSpace) + ":" + part(item);
}
QString normalizedPath(QString path) {
    path.replace('\\','/');
    while (path.endsWith('/')) path.chop(1);
    return invariantLower(path);
}
QString executableKey(QString identity) {
    identity = identity.trimmed();
    if (identity.isEmpty()) return {};
    const bool windowsPath = QRegularExpression("^[a-zA-Z]:[/\\\\]").match(identity).hasMatch() || identity.startsWith("\\\\");
    if (!windowsPath) {
        identity.replace('\\','/');
        if (QDir::isAbsolutePath(identity)) identity = QDir::cleanPath(identity);
        else identity = identity.section('/',-1);
    }
    return "exe:" + QString::fromLatin1(QCryptographicHash::hash(normalizedPath(identity).toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool excludedExecutable(QString name) {
    static const QSet<QString> names = {
        "applicationframehost.exe","battle.net.exe","chrome.exe","cmd.exe","code.exe","conhost.exe",
        "devenv.exe","discord.exe","dwm.exe","easyanticheat.exe","easyanticheat_eos.exe",
        "epicgameslauncher.exe","explorer.exe","firefox.exe","gamebar.exe","gamingservices.exe",
        "msedge.exe","notepad.exe","notepad++.exe","obs.exe","obs64.exe","powershell.exe","pwsh.exe",
        "riotclientservices.exe","searchhost.exe","shellexperiencehost.exe","steam.exe","steamwebhelper.exe",
        "streamboard.companion.exe","taskmgr.exe","teams.exe","upc.exe","uplaywebcore.exe","windowsterminal.exe","winword.exe"
    };
    name.replace('\\','/');
    return names.contains(name.section('/',-1).toLower());
}
static QString unescape(QString value) {
    return value.replace("\\\\","\\").replace("\\\"","\"");
}
QHash<QString,QString> parseVdf(const QString &text) {
    static const QRegularExpression re(R"vdf("((?:\\.|[^"\\])*)"\s+"((?:\\.|[^"\\])*)")vdf");
    QHash<QString,QString> result;
    auto it = re.globalMatch(text);
    while(it.hasNext()) { const auto m = it.next(); result.insert(unescape(m.captured(1)).toLower(),unescape(m.captured(2))); }
    return result;
}
QStringList steamLibraries(const QString &text) {
    static const QRegularExpression re(R"vdf("path"\s+"((?:\\.|[^"\\])*)")vdf",QRegularExpression::CaseInsensitiveOption);
    QStringList paths;
    auto it = re.globalMatch(text);
    while(it.hasNext()) { auto path = unescape(it.next().captured(1)).trimmed(); if(!path.isEmpty()) paths.append(path); }
    paths.removeDuplicates(); return paths;
}
std::optional<CatalogEntry> steamManifest(const QString &text, const QString &steamApps) {
    const auto v = parseVdf(text);
    const QString id = steamKey(v.value("appid")), name = v.value("name").trimmed(), dir = v.value("installdir").trimmed();
    if(id.isEmpty() || name.isEmpty() || dir.isEmpty()) return {};
    return CatalogEntry{{id,"steam",name,{}},QDir(steamApps).filePath("common/" + dir),{}};
}
std::optional<CatalogEntry> epicManifest(const QByteArray &json) {
    const auto v = QJsonDocument::fromJson(json).object();
    auto ns = v.value("CatalogNamespace").toString(v.value("Namespace").toString()).trimmed();
    auto item = v.value("CatalogItemId").toString(v.value("ItemId").toString()).trimmed();
    if(ns.isEmpty()) ns = v.value("Namespace").toString().trimmed();
    if(item.isEmpty()) item = v.value("ItemId").toString().trimmed();
    auto name = v.value("DisplayName").toString().trimmed(), dir = v.value("InstallLocation").toString().trimmed();
    auto launch = v.value("LaunchExecutable").toString().trimmed();
    if(ns.isEmpty() || item.isEmpty() || name.isEmpty() || dir.isEmpty()) return {};
    if(!launch.isEmpty()) { launch.replace('\\','/'); if(!QDir::isAbsolutePath(launch)) launch = QDir(dir).filePath(launch); }
    return CatalogEntry{{epicKey(ns,item),"epic",name,{}},dir,launch};
}
std::optional<Game> resolveGame(const Process &p, QVector<CatalogEntry> catalog, const QHash<QString,QString> &manual) {
    QString exe = p.name;
    exe.replace('\\','/'); exe = invariantLower(exe.section('/',-1).trimmed());
    if(exe.isEmpty()) { auto path = p.path; path.replace('\\','/'); exe = invariantLower(path.section('/',-1)); }
    if(exe.isEmpty()) return {};
    if(!exe.endsWith(".exe")) exe += ".exe";
    if(excludedExecutable(exe)) return {};
    const auto key = executableKey(p.path.isEmpty() ? exe : p.path);
    if(manual.contains(key)) return Game{key,"exe",manual.value(key),exe};
    if(!p.path.isEmpty()) {
        std::stable_sort(catalog.begin(),catalog.end(),[](const auto &a,const auto &b) {
            const auto al = normalizedPath(a.directory).size(), bl = normalizedPath(b.directory).size();
            return al == bl ? a.game.source == "steam" && b.game.source != "steam" : al > bl;
        });
        const auto path = normalizedPath(QDir::cleanPath(QString(p.path).replace('\\','/')));
        for(const auto &entry : catalog) {
            const auto dir = normalizedPath(QDir::cleanPath(QString(entry.directory).replace('\\','/')));
            if((!entry.launchExecutable.isEmpty() && path == normalizedPath(entry.launchExecutable)) || path.startsWith(dir + '/')) {
                auto game = entry.game; game.executable = exe; return game;
            }
        }
    }
    static const QSet<QString> generic = {"microsoft® windows® operating system","microsoft windows","application","game","unity player"};
    QString name;
    for(const auto &value : {p.productName,p.description}) {
        if(!value.trimmed().isEmpty() && !generic.contains(value.trimmed().toLower())) { name = value.trimmed().left(140); break; }
    }
    const bool needsName = name.isEmpty();
    if(needsName) { name = exe.left(exe.size()-4); name.replace(QRegularExpression("[_-]+")," "); }
    return Game{key,"exe",name,exe,needsName};
}
QVector<MappingRow> mappingRows(const QJsonArray &mappings) {
    QHash<QString,MappingRow> grouped;
    for(const auto &v : mappings) {
        const auto item = v.toObject(); const auto game = Game::fromJson(item["game"].toObject());
        if(game.key.isEmpty()) continue;
        auto &row = grouped[game.key]; row.game = game;
        const auto platform = item["platform_id"].toString();
        if(platform == "twitch") row.twitch = item["category"].toObject();
        if(platform == "vkplaylive") row.vk = item["category"].toObject();
    }
    QVector<MappingRow> rows;
    for(auto it = grouped.cbegin(); it != grouped.cend(); ++it) rows.append(it.value());
    std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b) {
        const int order = QString::compare(a.game.name,b.game.name,Qt::CaseInsensitive);
        return order == 0 ? a.game.key < b.game.key : order < 0;
    });
    return rows;
}
std::optional<Game> DetectionState::observe(const std::optional<Game> &game, qint64 now) {
    if(!game || game->needsName) { reset(); return {}; }
    if(!candidate || candidate->key != game->key) { candidate = game; since = now; lastSent = -1; return {}; }
    candidate = game;
    if(now - since < 10000 || (lastSent >= 0 && now - lastSent < 30000)) return {};
    lastSent = now; return candidate;
}
void DetectionState::reset() { candidate.reset(); since = 0; lastSent = -1; }
void DetectionState::accelerate() { lastSent = -1; }
bool DetectionState::stable(qint64 now) const { return candidate && now - since >= 10000; }
}
