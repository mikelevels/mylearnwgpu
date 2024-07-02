#include "webgpu-utils.h"

#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
    #include <webgpu/wgpu.h>
#endif

#include <iostream>
#include <vector>
#include <cassert>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

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

    //Get the queue to send data and commands to the GPU
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    //Set up a callback to be executed once all queued work is done.
    auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /*pUserData*/){
        std::cout<<"Queued work finished with status: "<<status<<std::endl;
    };
    wgpuQueueOnSubmittedWorkDone(queue,onQueueWorkDone,nullptr /*pUserData*/);

    //Create a command encoder, that will then build the command buffer
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = "My command encoder";// Pretty confident this could be anything
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Send some commands
    wgpuCommandEncoderInsertDebugMarker(encoder, "Do one thing");
    wgpuCommandEncoderInsertDebugMarker(encoder, "Do another thing");

    // Create the buffer by finish()-ing the encoder
    WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label = "Command buffer";
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
    wgpuCommandEncoderRelease(encoder);//release the encoder after it is finished

    //Submit the command queue
    std::cout<<"Submitting command..."<<std::endl;
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);
    std::cout<<"Command submitted."<<std::endl;

    for(int i=0; i<5; ++i){
        std::cout<<"Tick/Poll device..."<<std::endl;
#if defined(WEBGPU_BACKEND_DAWN)
        wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
        wgpuDevicePoll(device, false, nullptr);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
        emscripten_sleep(100);
#endif
    }

    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    return 0;
}
