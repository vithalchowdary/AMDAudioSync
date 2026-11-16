//
//  AMDAudioSync.cpp
//  AMDAudioSync
//

#include "AMDAudioSync.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_util.hpp>

AMDAudioSync *AMDAudioSync::callbackInstance = nullptr;

// DCN MMIO register dword offsets
static constexpr UInt32 kDcnBase2Dword             = 0x34C0;
static constexpr UInt32 kDigAfmtAudioSrcControlOff = 0x20AD;
static constexpr UInt32 kDigBeCntlOff              = 0x20B0;
static constexpr UInt32 kDigBlockStrideDword       = 0x100;

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

    // 1. Optional hooks on Display Core stream encoder audio setup using routeMultiplelilu
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

    // 2. Primary hook on AMDRadeonX6000_AmdRadeonFramebuffer::reportCapabilities_LinkInfo
    KernelPatcher::RouteRequest linkInfoReq {
        "__ZN35AMDRadeonX6000_AmdRadeonFramebuffer27reportCapabilities_LinkInfoEv",
        wrapReportCapabilitiesLinkInfo,
        orgReportCapabilitiesLinkInfo
    };

    if (!patcher.routeMultiple(index, &linkInfoReq, 1, address, size)) {
        SYSLOG("ADAS", "Failed to route reportCapabilities_LinkInfo");
        patcher.clearError();
    } else {
        SYSLOG("ADAS", "Successfully routed reportCapabilities_LinkInfo");
    }
}

void AMDAudioSync::wrapDceSeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    auto *self = callbackInstance;
    if (self != nullptr && streamEnc != nullptr) {
        const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
        if (azInst < 6) {
            for (size_t i = 0; i < 8; i++) {
                self->activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
                self->hasCapturedStreamSetup[i] = true;
            }
            DBGLOG("ADAS", "Captured dce110_se_audio_setup: az_inst=%u", azInst);
        }
        if (self->orgDce110SeAudioSetup != 0) {
            FunctionCast(wrapDceSeAudioSetup, self->orgDce110SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
        }
    }
}

void AMDAudioSync::wrapEnc1SeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo) {
    auto *self = callbackInstance;
    if (self != nullptr && streamEnc != nullptr) {
        const auto azInst = static_cast<UInt32>(reinterpret_cast<uintptr_t>(audioInfo));
        if (azInst < 6) {
            for (size_t i = 0; i < 8; i++) {
                self->activeAzaliaEndpointForDig[i] = static_cast<UInt8>(azInst);
                self->hasCapturedStreamSetup[i] = true;
            }
            DBGLOG("ADAS", "Captured enc1_se_audio_setup: az_inst=%u", azInst);
        }
        if (self->orgEnc1SeAudioSetup != 0) {
            FunctionCast(wrapEnc1SeAudioSetup, self->orgEnc1SeAudioSetup)(streamEnc, audioInfo, audioStreamSource, crtcInfo);
        }
    }
}

IOPCIDevice *AMDAudioSync::findGpuPciDevice(IOService *service) {
    IOService *curr = service;
    for (int depth = 0; curr != nullptr && depth < 8; depth++) {
        if (auto *pci = OSDynamicCast(IOPCIDevice, curr)) {
            if (pci->configRead16(kIOPCIConfigVendorID) == 0x1002) {
                return pci;
            }
        }
        curr = curr->getProvider();
    }
    return nullptr;
}

bool AMDAudioSync::queryActiveEndpointFromMMIO(IOService *fbService, UInt32 digIndex, UInt8 &outEndpointIndex) {
    if (digIndex >= 6) {
        return false;
    }

    IOPCIDevice *gpuPci = findGpuPciDevice(fbService);
    if (gpuPci == nullptr) {
        return false;
    }

    IOMemoryMap *mmioMap = gpuPci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress5, kIOMapInhibitCache);
    if (mmioMap == nullptr) {
        mmioMap = gpuPci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2, kIOMapInhibitCache);
    }
    if (mmioMap == nullptr) {
        return false;
    }

    const auto baseAddr = mmioMap->getVirtualAddress();
    const auto mapLen   = mmioMap->getLength();

    const UInt32 digBaseDword   = kDcnBase2Dword + (kDigBlockStrideDword * digIndex);
    const UInt64 digBeByteOff   = static_cast<UInt64>(digBaseDword + kDigBeCntlOff) * 4ULL;
    const UInt64 digAfmtByteOff = static_cast<UInt64>(digBaseDword + kDigAfmtAudioSrcControlOff) * 4ULL;

    bool resolved = false;
    if (baseAddr != 0 && (digBeByteOff + 4) <= mapLen && (digAfmtByteOff + 4) <= mapLen) {
        const UInt32 digBeVal   = *reinterpret_cast<volatile const UInt32 *>(baseAddr + digBeByteOff);
        const UInt32 digAfmtVal = *reinterpret_cast<volatile const UInt32 *>(baseAddr + digAfmtByteOff);

        const UInt8 epIdx = static_cast<UInt8>(digAfmtVal & 0x7U);
        if (digBeVal != 0 && digBeVal != 0xFFFFFFFFU && epIdx < 6) {
            outEndpointIndex = epIdx;
            resolved = true;
            DBGLOG("ADAS", "MMIO DIG%u: BE_CNTL=0x%08X, AFMT_SRC=0x%08X -> ep=%u",
                   digIndex, digBeVal, digAfmtVal, epIdx);
        }
    }

    mmioMap->release();
    return resolved;
}

IOReturn AMDAudioSync::wrapReportCapabilitiesLinkInfo(void *self) {
    auto *inst = callbackInstance;
    if (inst == nullptr || inst->orgReportCapabilitiesLinkInfo == 0) {
        return kIOReturnSuccess;
    }

    const IOReturn ret = FunctionCast(wrapReportCapabilitiesLinkInfo, inst->orgReportCapabilitiesLinkInfo)(self);

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

    // Optional boot-arg override
    UInt32 manualPin = 0;
    if (PE_parse_boot_argn("amdhdmipin", &manualPin, sizeof(manualPin)) &&
        manualPin >= 3 && manualPin <= 15 && (manualPin % 2) == 1) {
        targetPin = static_cast<UInt8>(manualPin);
    } else if (appleStaticPin >= 0x03 && appleStaticPin <= 0x0F && (appleStaticPin % 2) == 1) {
        const UInt32 transmitterDigIdx = (appleStaticPin - 3U) / 2U;

        if (transmitterDigIdx < 8 && inst->hasCapturedStreamSetup[transmitterDigIdx]) {
            const UInt8 dcEndpoint = inst->activeAzaliaEndpointForDig[transmitterDigIdx];
            targetPin = static_cast<UInt8>(3U + (2U * dcEndpoint));
        } else {
            UInt8 mmioEndpoint = 0;
            if (queryActiveEndpointFromMMIO(svc, transmitterDigIdx, mmioEndpoint)) {
                targetPin = static_cast<UInt8>(3U + (2U * mmioEndpoint));
            } else if (IOPCIDevice *gpuPci = findGpuPciDevice(svc)) {
                const UInt16 devId = gpuPci->configRead16(kIOPCIConfigDeviceID);
                if ((devId == 0x15DD || devId == 0x15D8) && appleStaticPin == 0x05) {
                    targetPin = 0x03;
                }
            }
        }
    }

    
    if (targetPin != appleStaticPin) {
        UInt8 newInfo[4] = {bytes[0], bytes[1], targetPin, bytes[3]};
        svc->setProperty("audio-codec-info", newInfo, sizeof(newInfo));
        SYSLOG("ADAS", "Synced audio-codec-info Pin NID from 0x%02X to 0x%02X", appleStaticPin, targetPin);
    }

    return ret;
}