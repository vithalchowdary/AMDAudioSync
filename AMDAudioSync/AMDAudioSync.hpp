//
//  AMDAudioSync.hpp
//  AMDAudioSync
//

#pragma once

#include <Headers/kern_patcher.hpp>
#include <IOKit/IOService.h>
#include <libkern/c++/OSData.h>

class AMDAudioSync {
public:
    static void init();
<<<<<<< HEAD
    static void deinit();
=======
>>>>>>> d3897b7 (test)

private:
    static void onKextLoad(void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    static void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

    static IOReturn wrapReportCapabilitiesLinkInfo(void *self);
<<<<<<< HEAD
    static void wrapDceSeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo);
    static void wrapEnc1SeAudioSetup(void *streamEnc, void *audioInfo, UInt32 audioStreamSource, void *crtcInfo);
=======
>>>>>>> d3897b7 (test)
};
