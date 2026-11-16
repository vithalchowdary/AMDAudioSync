//
//  AMDAudioSync.cpp
//  AMDAudioSync
//

#include "AMDAudioSync.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>

static const char *kextX6000FBPath[] {
    "/System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer"
};

static KernelPatcher::KextInfo kextX6000FB {
    "com.apple.kext.AMDRadeonX6000Framebuffer",
    kextX6000FBPath,
    arrsize(kextX6000FBPath),
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

static mach_vm_address_t orgReportCapabilitiesLinkInfo = 0;
static mach_vm_address_t orgDce110SeAudioSetup = 0;
static mach_vm_address_t orgEnc1SeAudioSetup = 0;

static UInt8 activeAzaliaEndpointForDig[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static bool hasCapturedStreamSetup[8] = {false, false, false, false, false, false, false, false};

void AMDAudioSync::init() {
    auto err = lilu.onKextLoad(&kextX6000FB, 1, onKextLoad, nullptr);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("ADAS", "Failed to register framebuffer callback (%d)", err);
    } else {
        DBGLOG("ADAS", "Registered framebuffer callback");
    }
}

void AMDAudioSync::deinit() {}

void AMDAudioSync::onKextLoad(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    processKext(patcher, index, address, size);
}

void AMDAudioSync::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextX6000FB.loadIndex != index) {
        return;
    }

    KernelPatcher::RouteRequest dceReq {
        "_dce110_se_audio_setup",
        wrapDceSeAudioSetup,
        orgDce110SeAudioSetup
    };
    if (!patcher.routeMultiple(index, &dceReq, 1, address, size)) {
        patcher.clearError();
    }

    KernelPatcher::RouteRequest enc1Req {
        "_enc1_se_audio_setup",
        wrapEnc1SeAudioSetup,
        orgEnc1SeAudioSetup
    };
    if (!patcher.routeMultiple(index, &enc1Req, 1, address, size)) {
        patcher.clearError();
    }

    KernelPatcher::RouteRequest linkInfoReq {
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer27reportCapabilities_LinkInfoEv",
        wrapReportCapabilitiesLinkInfo,
        orgReportCapabilitiesLinkInfo
    };

    if (!patcher.routeMultipleLong(index, &linkInfoReq, 1, address, size)) {
        SYSLOG("ADAS", "Failed to route reportCapabilities_LinkInfo");
        patcher.clearError();
    } else {
        SYSLOG("ADAS", "Routed reportCapabilities_LinkInfo");
    }
}

void AMDAudioSync::wrapDceSeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
    if (azInst < 6) {
        for (size_t i = 0; i < 8; i++) {
            activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
            hasCapturedStreamSetup[i] = true;
        }
    }
    if (orgDce110SeAudioSetup != 0) {
        FunctionCast(wrapDceSeAudioSetup, orgDce110SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
    }
}

void AMDAudioSync::wrapEnc1SeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
    if (azInst < 6) {
        for (size_t i = 0; i < 8; i++) {
            activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
            hasCapturedStreamSetup[i] = true;
        }
    }
    if (orgEnc1SeAudioSetup != 0) {
        FunctionCast(wrapEnc1SeAudioSetup, orgEnc1SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
    }
}

IOReturn AMDAudioSync::wrapReportCapabilitiesLinkInfo(void *self) {
    if (orgReportCapabilitiesLinkInfo == 0) {
        return kIOReturnSuccess;
    }

    const IOReturn ret = FunctionCast(wrapReportCapabilitiesLinkInfo, orgReportCapabilitiesLinkInfo)(self);
    if (self == nullptr) {
        return ret;
    }

    auto *svc = OSDynamicCast(IOService, static_cast<OSMetaClassBase *>(self));
    if (svc == nullptr) {
        return ret;
    }

    auto *prop = OSDynamicCast(OSData, svc->getProperty("audio-codec-info"));
    if (prop == nullptr || prop->getLength() != 4) {
        return ret;
    }

    const auto *bytes = static_cast<const UInt8 *>(prop->getBytesNoCopy());
    if (bytes == nullptr || bytes[1] != 0x01) {
        return ret;
    }

    const UInt8 appleStaticPin = bytes[2];
    UInt8 targetPin = appleStaticPin;

    UInt32 manualPin = 0;
    if (PE_parse_boot_argn("amdhdmipin", &manualPin, sizeof(manualPin)) &&
        manualPin >= 3 && manualPin <= 15 && (manualPin % 2) == 1) {
        targetPin = static_cast<UInt8>(manualPin);
    } else if (checkKernelArgument("-NRedHDMIPin3")) {
        targetPin = 0x03;
    } else if (appleStaticPin >= 0x03 && appleStaticPin <= 0x0F && (appleStaticPin % 2) == 1) {
        const UInt32 transmitterDigIdx = (appleStaticPin - 3U) / 2U;

        if (transmitterDigIdx < 8 && hasCapturedStreamSetup[transmitterDigIdx]) {
            const UInt8 dcEndpoint = activeAzaliaEndpointForDig[transmitterDigIdx];
            targetPin = static_cast<UInt8>(3U + (2U * dcEndpoint));
        } else if (appleStaticPin == 0x05) {
            targetPin = 0x03;
        }
    }

    if (targetPin != appleStaticPin) {
        UInt8 newInfo[4] = {bytes[0], bytes[1], targetPin, bytes[3]};
        svc->setProperty("audio-codec-info", newInfo, sizeof(newInfo));
        SYSLOG("ADAS", "Synced audio-codec-info Pin NID from 0x%02X to 0x%02X", appleStaticPin, targetPin);
    }

    return ret;
}
