//
//  AMDAudioSync.cpp
//  AMDAudioSync
//
//  Rewrites the HDA pin NID inside the "audio-codec-info" property that
//  AMDRadeonX6000Framebuffer publishes for each display link.
//
//  Boot-args:
//    amdhdmipin=N   force HDA pin NID N (valid HDMI/DP pins on Raven/Picasso: 3, 5, 7, 9)
//    -NRedHDMIPin3  old spelling of amdhdmipin=3, kept so existing boot-args keep working
//  With neither, nothing is changed; the value Apple chose is only logged.
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

static bool isValidPin(UInt32 pin) {
    return pin >= 3 && pin <= 15 && (pin % 2) == 1;
}

void AMDAudioSyncHandler::init() {
    auto err = lilu.onKextLoad(&kextX6000FB, 1, onKextLoad, nullptr);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("ADAS", "Failed to register framebuffer callback (%d)", err);
    } else {
        SYSLOG("ADAS", "Registered framebuffer callback");
    }
}

void AMDAudioSyncHandler::onKextLoad(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    processKext(patcher, index, address, size);
}

void AMDAudioSyncHandler::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextX6000FB.loadIndex != index) {
        return;
    }

    SYSLOG("ADAS", "AMDRadeonX6000Framebuffer loaded, routing reportCapabilities_LinkInfo");

    KernelPatcher::RouteRequest linkInfoReq {
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer27reportCapabilities_LinkInfoEv",
        wrapReportCapabilitiesLinkInfo,
        orgReportCapabilitiesLinkInfo
    };

    if (!patcher.routeMultipleLong(index, &linkInfoReq, 1, address, size)) {
        SYSLOG("ADAS", "Failed to route reportCapabilities_LinkInfo (patcher error %d)", patcher.getError());
        patcher.clearError();
    } else {
        SYSLOG("ADAS", "Routed reportCapabilities_LinkInfo");
    }
}

IOReturn AMDAudioSyncHandler::wrapReportCapabilitiesLinkInfo(void *self) {
    // The route only exists if the original was found, so this should never be 0.
    if (orgReportCapabilitiesLinkInfo == 0) {
        return kIOReturnSuccess;
    }

    const IOReturn ret = FunctionCast(wrapReportCapabilitiesLinkInfo, orgReportCapabilitiesLinkInfo)(self);
    if (self == nullptr) {
        return ret;
    }

    auto *svc = OSDynamicCast(IOService, static_cast<OSMetaClassBase *>(self));
    if (svc == nullptr) {
        SYSLOG("ADAS", "framebuffer object is not an IOService");
        return ret;
    }

    auto *prop = OSDynamicCast(OSData, svc->getProperty("audio-codec-info"));
    if (prop == nullptr || prop->getLength() != 4) {
        SYSLOG("ADAS", "no 4-byte audio-codec-info on this framebuffer");
        return ret;
    }

    const auto *bytes = static_cast<const UInt8 *>(prop->getBytesNoCopy());
    if (bytes == nullptr) {
        return ret;
    }

    const UInt8 appleStaticPin = bytes[2];
    SYSLOG("ADAS", "audio-codec-info = %02X %02X %02X %02X", bytes[0], bytes[1], bytes[2], bytes[3]);

    if (bytes[1] != 0x01) {
        return ret;    // not the layout we understand
    }

    UInt32 manualPin = 0;
    UInt8 targetPin = appleStaticPin;
    if (PE_parse_boot_argn("amdhdmipin", &manualPin, sizeof(manualPin))) {
        if (isValidPin(manualPin)) {
            targetPin = static_cast<UInt8>(manualPin);
        } else {
            SYSLOG("ADAS", "ignoring amdhdmipin=%u (use 3, 5, 7 or 9)", manualPin);
        }
    } else if (checkKernelArgument("-NRedHDMIPin3")) {
        targetPin = 0x03;
    } else {
        SYSLOG("ADAS", "Apple pin NID is 0x%02X; add amdhdmipin=3 (or 5/7/9) to boot-args to override", appleStaticPin);
    }

    if (targetPin != appleStaticPin) {
        UInt8 newInfo[4] = {bytes[0], bytes[1], targetPin, bytes[3]};
        svc->setProperty("audio-codec-info", newInfo, sizeof(newInfo));
        SYSLOG("ADAS", "Changed audio-codec-info Pin NID from 0x%02X to 0x%02X", appleStaticPin, targetPin);
    }

    return ret;
}
