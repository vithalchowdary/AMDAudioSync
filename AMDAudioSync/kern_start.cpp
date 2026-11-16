//
//  kern_start.cpp
//
//  Standalone Lilu plugin that dynamically resolves the active AMD Display Core
//  Azalia audio endpoint (dce_aud->inst) and syncs Apple's `audio-codec-info`
//  IORegistry property on AMDRadeonX6000Framebuffer.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include "AMDAudioSync.hpp"

static AMDAudioSync audioFixInstance;

const char *bootargOff[] {
    "-amdasoff"
};

const char *bootargDebug[] {
    "-amdasdbg"
};

const char *bootargBeta[] {
    "-amdasbeta"
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
        audioFixInstance.init();
    }
};
