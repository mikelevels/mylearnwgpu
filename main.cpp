#include "webgpu-utils.h"

#include <webgpu/webgpu.h>
#include <iostream>
#include <vector>
#include <cassert>

int main(int, char**){
    // Generate a descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
#ifdef WEBGPU_BACKEND_DAWN
    // Make sure the uncaptured error callback is called as soon as an error
    // occurs rather than at the next call to "wgpuDeviceTick".
    WGPUDawnTogglesDescriptor toggles;
    toggles.chain.next = nullptr;
    toggles.chain.sType = WGPUSType_DawnTogglesDescriptor;
    toggles.disabledToggleCount = 0;
    toggles.enabledToggleCount = 1;
    const char* toggleName = "enable_immediate_error_handling";
    toggles.enabledToggles = &toggleName;

    desc.nextInChain = &toggles.chain;
#endif

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
    WGPUInstance instance = wgpuCreateInstance(nullptr);
#else // WEBGPU_BACKEND_EMSCRIPTEN
    // Create the instance using the descriptor
    WGPUInstance instance = wgpuCreateInstance(&desc);
#endif // WEBGPU_BACKEND_EMSCRIPTEN

    std::cout << "WGPU instance: "<< instance << std::endl;

    if(!instance){
        std::cerr<< "Could not initialize WebGPU!"<<std::endl;
        return 1;
    }

    // We can check whether there is actually an instance called
    if(!instance){
        std::cerr<< "Could not initialize WebGPU!"<<std::endl;
        return 1;
    }

    std::cout<< "Requesting adapter..."<<std::endl;

    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain = nullptr;
    WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);

    std::cout<<"Got adapter: "<<adapter<<std::endl;

    // Release the instance. It is no longer explicitly used. The instance persists
    // until the adapter gets destroyed.
    wgpuInstanceRelease(instance);

    inspectAdapter(adapter);

    std::cout << "Requesting device..." <<std::endl;

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;
    deviceDesc.label = "My Device";// Can put anything in here!
    deviceDesc.requiredFeatureCount = 0;// Do not require any specific features for the sake of this tutorial
    deviceDesc.requiredLimits = nullptr; // Do not require any specific limits
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label = "The default queue";

    //A function that is invoked whenever the device stops being available.
    deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const * message, void* /*pUserData*/){
        std::cout<< "Device lost: reason "<<reason;
        if(message) std::cout<<"("<<message<<")";
        std::cout<<std::endl;
    };;

    WGPUDevice device = requestDeviceSync(adapter, &deviceDesc);

    std::cout<<"Got device: "<<device<<std::endl;

    //We can already release the adapter since we no longer need to use it
    //The underlying adapter will keep existing until the underlying device
    // get destroyed.
    wgpuAdapterRelease(adapter);

    inspectDevice(device);

    auto onDeviceError = [](WGPUErrorType type, char const* message, void* /*pUserData*/){
        std::cout<<"Uncaptured device error: type "<<type;
        if(message) std::cout << "(" << message<<")";
        std::cout<<std::endl;
    };
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr/*pUserData*/);

    wgpuDeviceRelease(device);
    return 0;
}
