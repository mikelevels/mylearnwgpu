#include "webgpu-utils.h"

#include <glfw3webgpu.h>

#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
    #include <webgpu/wgpu.h>
#endif

#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <cassert>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

/**
 * Main application class, that holds the whole app state and regroups
 * init/main loop/terminate functions.
 */
class Application{
    public:
        //Initialize everything return true on success
        bool Initialize();

        //Uninitialize everything
        void Terminate();

        // Draw a frame, handle events
        void MainLoop();

        // Return true as long as the main loop should keep running
        bool IsRunning();

    private:
        // All variables shared in the public interface to this class
        GLFWwindow *window;
        WGPUSurface surface;
        WGPUDevice device;
        WGPUQueue queue;
};

bool Application::Initialize(){
    std::cout<<"Opening window..."<<std::endl;

    if(!glfwInit()){
        std::cerr<<"Could not initialize GLFW!"<<std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

    if(!window){
        std::cerr <<"Could not open window!"<<std::endl;
        glfwTerminate();
        return 1;
    }

    std::cout << "GLFW window: "<< window<<std::endl;

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

    std::cout<< "Getting surface..."<<std::endl;
    surface = glfwGetWGPUSurface(instance, window);
    std::cout<< "WGPU surface: "<<surface<<std::endl;

    std::cout << "Requesting adapter..." << std::endl;

	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = nullptr;
	adapterOpts.compatibleSurface = surface;
	//                              ^^^^^^^ Use the surface here
	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);
    // Release the instance. It is no longer explicitly used. The instance persists
    // until the adapter gets destroyed. THIS DOES NOT NEED TO OCCUR IN TERMINATE
	wgpuInstanceRelease(instance);

	std::cout << "Got adapter: " << adapter << std::endl;

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

    // THIS IS A CLASS MEMBER NOW, it is initialized/called without the type because it is a class member now
    device = requestDeviceSync(adapter, &deviceDesc);

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

    //Get the queue to send data and commands to the GPU, THIS IS A CLASS MEMBER NOW
    queue = wgpuDeviceGetQueue(device);

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

    return true;
}

void Application::Terminate(){
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuSurfaceRelease(surface);
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop(){
    //Check whether the user clicked on the close button (and any other
    // mouse/key event, which we don't use so far)
    glfwPollEvents();

    //Also move here the tick/poll but NOT the emscripten sleep
#if defined(WEBGPU_BACKEND_DAWN)
    wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
    wgpuDevicePoll(device, false, nullptr);
#endif
}

bool Application::IsRunning(){
    return !glfwWindowShouldClose(window);
}

int main(int, char**){
    Application app;

    if(!app.Initialize()){
        return 1;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void * arg){
        Application * pApp = reinterpret_cast<Application*>(arg);
        pApp->MainLoop();
    }, &app, 0, true);
#else
    while(app.IsRunning()){
        app.MainLoop();
    }
#endif

    return 0;
}
