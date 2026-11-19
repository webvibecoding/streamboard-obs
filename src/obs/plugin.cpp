// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QPointer>
#include <QTimer>
#include "dock.h"
#include "version.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("streamboard-obs","en-US")
MODULE_EXPORT const char *obs_module_description(void) { return "Streamboard foreground game category synchronization"; }
MODULE_EXPORT const char *obs_module_name(void) { return "Streamboard OBS"; }
MODULE_EXPORT const char *obs_module_author(void) { return "Webvibecoding"; }
static QPointer<sb::Dock> dock;
static bool frontendActive = false;
static void frontendEvent(enum obs_frontend_event,void *);
static void shutdown() {
    if(dock) dock->shutdown();
    if(frontendActive) {
        obs_frontend_remove_event_callback(frontendEvent,nullptr);
        obs_frontend_remove_dock("streamboard-obs"); frontendActive = false;
    }
    dock = nullptr;
}
static void frontendEvent(enum obs_frontend_event event,void *) {
    if(event == OBS_FRONTEND_EVENT_EXIT) { shutdown(); blog(LOG_INFO,"[Streamboard OBS] shutdown complete"); }
    else if(event == OBS_FRONTEND_EVENT_STREAMING_STARTED && dock) dock->streamStarted();
}
bool obs_module_load(void) {
    if(!QString::fromUtf8(obs_get_version_string()).startsWith("33.")) {
        blog(LOG_ERROR,"[Streamboard OBS] OBS Studio 33.x is required"); return false;
    }
    return true;
}
void obs_module_post_load(void) {
    char *path = obs_module_config_path("");
    auto *widget = new sb::Dock(QString::fromUtf8(path)); bfree(path);
    if(!obs_frontend_add_dock_by_id("streamboard-obs","Streamboard",widget)) {
        delete widget; blog(LOG_ERROR,"[Streamboard OBS] dock registration failed"); return;
    }
    dock = widget; frontendActive = true;
    obs_frontend_add_event_callback(frontendEvent,nullptr);
    blog(LOG_INFO,"[Streamboard OBS] loaded version %s; dock registered",STREAMBOARD_VERSION);
    if(qEnvironmentVariableIsSet("STREAMBOARD_OBS_CI_SMOKE")) {
        QTimer::singleShot(1000,widget,[widget] {
            blog(widget->smokeCheck() ? LOG_INFO : LOG_ERROR,"[Streamboard OBS] CI dock check %s",widget->smokeCheck() ? "passed" : "FAILED");
        });
    }
}
void obs_module_unload(void) { shutdown(); }
