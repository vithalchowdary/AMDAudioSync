//
//  AMDAudioSync.cpp
//  AMDAudioSync
//

#include "AMDAudioSync.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_util.hpp>

AMDAudioSync *AMDAudioSync::callbackInstance = nullptr;

void AMDAudioSync::init() {
    callbackInstance = this;

    auto err = lilu.onKextLoad(&kextX6000FB, 1, onKextLoad, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("ADAS", "Failed to register framebuffer callback (%d)", err);
    } else {
        DBGLOG("ADAS", "Registered framebuffer callback");
    }
}

void AMDAudioSync::deinit() {}

void AMDAudioSync::onKextLoad(void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    auto *self = static_cast<AMDAudioSync *>(user);
    if (self != nullptr) {
        self->processKext(patcher, index, address, size);
    }
}

void AMDAudioSync::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextX6000FB.loadIndex != index) {
        return;
    }

    // 1. Hook Display Core stream encoder audio setup to dynamically capture pipe_ctx->stream_res.audio->inst
    KernelPatcher::RouteRequest dceReq {
        "_dce110_se_audio_setup",
        wrapDceSeAudioSetup,
        orgDce110SeAudioSetup
    };
    if (patcher.routeMultiple(index, &dceReq, 1, address, size)) {
        SYSLOG("ADAS", "Routed _dce110_se_audio_setup");
    } else {
        patcher.clearError();
    }

    KernelPatcher::RouteRequest enc1Req {
        "_enc1_se_audio_setup",
        wrapEnc1SeAudioSetup,
        orgEnc1SeAudioSetup
    };
    if (patcher.routeMultiple(index, &enc1Req, 1, address, size)) {
        SYSLOG("ADAS", "Routed _enc1_se_audio_setup");
    } else {
        patcher.clearError();
    }

    // 2. Hook AMDRadeonX6000_AmdRadeonFramebuffer::reportCapabilities_LinkInfo()
    // Use routeMultipleLong so it safely chains even if another kext routed nearby symbols
    KernelPatcher::RouteRequest linkInfoReq {
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer27reportCapabilities_LinkInfoEv",
        wrapReportCapabilitiesLinkInfo,
        orgReportCapabilitiesLinkInfo
    };

    if (!patcher.routeMultipleLong(index, &linkInfoReq, 1, address, size)) {
        SYSLOG("ADAS", "Failed to route reportCapabilities_LinkInfo");
        patcher.clearError();
    } else {
        SYSLOG("ADAS", "Successfully routed reportCapabilities_LinkInfo");
    }
}

void AMDAudioSync::wrapDceSeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    auto *self = callbackInstance;
    if (self != nullptr) {
        const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
        if (azInst < 6) {
            for (size_t i = 0; i < 8; i++) {
                self->activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
                self->hasCapturedStreamSetup[i] = true;
            }
            DBGLOG("ADAS", "Captured dce110_se_audio_setup: az_inst=%u -> Pin NID 0x%02X",
                   azInst, static_cast<UInt8>(3U + 2U * azInst));
        }
        if (self->orgDce110SeAudioSetup != 0) {
            FunctionCast(wrapDceSeAudioSetup, self->orgDce110SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
        }
    }
}

void AMDAudioSync::wrapEnc1SeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    auto *self = callbackInstance;
    if (self != nullptr) {
        const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
        if (azInst < 6) {
            for (size_t i = 0; i < 8; i++) {
                self->activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
                self->hasCapturedStreamSetup[i] = true;
            }
            DBGLOG("ADAS", "Captured enc1_se_audio_setup: az_inst=%u -> Pin NID 0x%02X",
                   azInst, static_cast<UInt8>(3U + 2U * azInst));
        }
        if (self->orgEnc1SeAudioSetup != 0) {
            FunctionCast(wrapEnc1SeAudioSetup, self->orgEnc1SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
        }
    }
}

IOReturn AMDAudioSync::wrapReportCapabilitiesLinkInfo(void *self) {
    auto *inst = callbackInstance;
    if (inst == nullptr || inst->orgReportCapabilitiesLinkInfo == 0) {
        return kIOReturnSuccess;
    }

    const IOReturn ret = FunctionCast(wrapReportCapabilitiesLinkInfo, inst->orgReportCapabilitiesLinkInfo)(self);
    if (self == nullptr) {
        return ret;
    }

    // AMDRadeonX6000_AmdRadeonFramebuffer inherits directly from IOFramebuffer -> IOGraphicsDevice -> IOService -> IORegistryEntry
    auto *regEntry = OSDynamicCast(IORegistryEntry, static_cast<OSObject *>(self));
    if (regEntry == nullptr) {
        return ret;
    }

    auto *prop = OSDynamicCast(OSData, regEntry->getProperty("audio-codec-info"));
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

        if (transmitterDigIdx < 8 && inst->hasCapturedStreamSetup[transmitterDigIdx]) {
            const UInt8 dcEndpoint = inst->activeAzaliaEndpointForDig[transmitterDigIdx];
            targetPin = static_cast<UInt8>(3U + (2U * dcEndpoint));
        } else if (appleStaticPin == 0x05) {
            // Default active first audio stream in Display Core (`dce_aud[0]`) is always endpoint 0 (Pin NID 0x03)
            targetPin = 0x03;
        }
    }

    if (targetPin != appleStaticPin) {
        UInt8 newInfo[4] = {bytes[0], bytes[1], targetPin, bytes[3]};
        regEntry->setProperty("audio-codec-info", newInfo, sizeof(newInfo));
        SYSLOG("ADAS", "Synced audio-codec-info Pin NID from 0x%02X to 0x%02X", appleStaticPin, targetPin);
    }

    return ret;
}