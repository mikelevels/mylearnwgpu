#include <webgpu/webgpu.h>
#include <iostream>
#include <vector>
#include <cassert>
#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif // __EMSCRIPTEN__

/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapterSync(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const * options){
    // A simple structure holding the local information shared with the
    // onAdapterRequestEnded callback.
    struct UserData{
        WGPUAdapter adapter=nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    // C++ lambda function, but could be any function defined in the
    //global scope. The lambda must not capture any variables in scope
    // thats why the square brackets are empty '[]'. This forces the 
    // lambda to behave like a regular C function pointer, this is what
    // `wgpuInstanceAdapter expects because it is a C API. Otherwise
    // do what you can to convey that you want to capture through the
    // pUserData pointer, provided as the last argument of the wgpu function
    // call and received by the callback as its last argument.
    // Here is the callback called by wgpuInstanceRequestAdapter:
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * pUserData){
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if(status == WGPURequestAdapterStatus_Success){
            userData.adapter = adapter;
        } else {
            std::cout << "Could not get WebGPU adapter:"<< message << std::endl;
        }
        userData.requestEnded = true;
    };

    //Call to the WebGPU request adapter procedure
    wgpuInstanceRequestAdapter(
        instance/* equivalent of navigator.gpu */,
        options,
        onAdapterRequestEnded,
        (void*)&userData
    );

#ifdef __EMSCRIPTEN__
    // We wait until userData.requestEnded gets true
	 while (!userData.requestEnded) {
	 	emscripten_sleep(100);
	 }
#endif // __EMSCRIPTEN__

	assert(userData.requestEnded);

	return userData.adapter;
};

void inspectAdapter(WGPUAdapter adapter){
#ifndef __EMSCRIPTEN__
    // Limits
    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain = nullptr;
    bool success  = wgpuAdapterGetLimits(adapter, &supportedLimits);
    if(success){
        std::cout<<"Adapter limits: "<<std::endl;
        std::cout<<" - maxTextureDimension1D: "<<supportedLimits.limits.maxTextureDimension1D <<std::endl;
        std::cout<<" - maxTextureDimension2D: "<<supportedLimits.limits.maxTextureDimension2D <<std::endl;
        std::cout<<" - maxTextureDimension3D: "<<supportedLimits.limits.maxTextureDimension3D <<std::endl;
        std::cout<<" - maxTextureArrayLayers: "<<supportedLimits.limits.maxTextureArrayLayers <<std::endl;
    }
#endif // NOT __EMSCRIPTEN__

    //Supported features
    std::vector<WGPUFeatureName> features;

    //Call the function a first time with a null return address, just to get
    // the entry count.
    size_t featureCount = wgpuAdapterEnumerateFeatures(adapter, nullptr);

    //Allocate memory(could be a new, or a malloc() if this were C)
    features.resize(featureCount);

    // Call the function a second time, with a non-null return address
    wgpuAdapterEnumerateFeatures(adapter,features.data());

    std::cout<< "Adapter features: "<<std::endl;
    std::cout<<std::hex;// Integers written as hexadecimeal to ease comparison with webgpu.h literals
    for(auto f : features){
        std::cout<<" - 0x"<<f<<std::endl;
    }
    std::cout << std::dec; //Output decimals again.

    // Print and store the properties
    WGPUAdapterProperties properties = {};
    properties.nextInChain = nullptr;
    wgpuAdapterGetProperties(adapter, &properties);
    std::cout << "Adapter properties:"<<std::endl;
    std::cout << " - vendorID: " << properties.vendorID << std::endl;
    if(properties.vendorName){
        std::cout<<" - vendorName: "<<properties.vendorName<<std::endl;
    }
    if(properties.architecture){
        std::cout<<" - architecture: "<<properties.architecture<<std::endl;
    }
    std::cout<<" - deviceID: "<<properties.deviceID<<std::endl;
    if(properties.name){
        std::cout<<" - name: "<<properties.name<<std::endl;
    }
    if(properties.driverDescription){
        std::cout<<" - driverDescription: " << properties.driverDescription <<std::endl;
    }
    std::cout << std::hex;
    std::cout << " - adapterType: 0x" << properties.adapterType << std::endl;
    std::cout << " - backendType: 0x" << properties.backendType <<std::endl;
    std::cout<<std::dec; // Back to decimals
};

int main(int, char**){
    // Generate a descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
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

    wgpuAdapterRelease(adapter);

    return 0;
}
