#include <webgpu/webgpu.h>
#include <iostream>

int main(int, char**){
    // Generate a descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;

    //Create the instance using this descriptor
    WGPUInstance instance = wgpuCreateInstance(&desc);
    
    // We can check whether there is actually an instance called
    if(!instance){
        std::cerr<< "Could not initialize WebGPU!"<<std::endl;
        return 1;
    }

    //Display the instance object
    std::cout<< "WGPU instance: "<<instance<<std::endl;

    //Delete the WebGPU instance
    wgpuInstanceRelease(instance);

    return 0;
}
