//
//  kern_start.cpp
//  AMDAudioSync
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include "AMDAudioSync.hpp"

const char *bootargOff[] {
    "-adasoff"
};

const char *bootargDebug[] {
    "-adasdbg"
};

const char *bootargBeta[] {
    "-adasbeta"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Catalina,
    KernelVersion::Tahoe,
    []() {
        AMDAudioSyncHandler::init();
    }
};
