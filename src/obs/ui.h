// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs-module.h>
#include <QString>
namespace sb {
inline QString text(const char *key) { return QString::fromUtf8(obs_module_text(key)); }
inline QString platformName(const QString &id) { return id == "twitch" ? "Twitch" : "VK Play Live"; }
inline QString errorText(const QString &error) {
    if(error == "desktop_pairing_required" || error == "desktop_device_revoked" || error == "invalid_grant") return text("ConnectRequired");
    if(error == "network_error") return text("NetworkError");
    if(error == "expired_token") return text("PairingExpired");
    if(error == "credential_store_failed") return text("CredentialError");
    if(error.contains("rate_limit") || error == "http_429" || error == "slow_down") return text("RateLimited");
    return text("RequestFailed") + " (" + error.left(80) + ")";
}
}
