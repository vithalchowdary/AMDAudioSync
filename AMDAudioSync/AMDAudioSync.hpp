//
//  AMDAudioSync.hpp
//  AMDAudioSync
//

#pragma once

#include <Headers/kern_patcher.hpp>
#include <IOKit/IOService.h>
#include <libkern/c++/OSData.h>

class AMDAudioSyncHandler {
public:
    static void init();

private:
    static void onKextLoad(void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    static void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

    static IOReturn wrapReportCapabilitiesLinkInfo(void *self);
};
