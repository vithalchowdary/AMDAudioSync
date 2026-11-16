//
//  AMDAudioSync.hpp
//  AMDAudioSync
//

#pragma once

#include <Headers/kern_patcher.hpp>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>

class AMDAudioSync {
public:
    void init();
    void deinit();

private:
    static AMDAudioSync *callbackInstance;

    KernelPatcher::KextInfo kextX6000FB {
        "com.apple.kext.AMDRadeonX6000Framebuffer",
        nullptr,
        0,
        {true, true},
        {},
        KernelPatcher::KextInfo::Unloaded
    };

    mach_vm_address_t orgReportCapabilitiesLinkInfo {0};
    mach_vm_address_t orgDce110SeAudioSetup {0};
    mach_vm_address_t orgEnc1SeAudioSetup {0};

    UInt8 activeAzaliaEndpointForDig[8] {0, 0, 0, 0, 0, 0, 0, 0};
    bool hasCapturedStreamSetup[8] {false, false, false, false, false, false, false, false};

    static void onKextLoad(void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

    static IOReturn wrapReportCapabilitiesLinkInfo(void *self);
    static void wrapDceSeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo);
    static void wrapEnc1SeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo);

    static bool queryActiveEndpointFromMMIO(IOService *fbService, UInt32 digIndex, UInt8 &outEndpointIndex);
    static IOPCIDevice *findGpuPciDevice(IOService *service);
};