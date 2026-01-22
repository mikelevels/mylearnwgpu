// Include the C++ wrapper instead of the raw header(s)
#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu/webgpu.hpp"


#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

#include <iostream>
#include <cassert>
#include <vector>

#include "ResourceManager.h"

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
        wgpu::TextureView GetNextSurfaceTextureView();
		//Substep of Initilize() that creates the render pipeline
		void InitializePipeline();
		wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;
		void InitializeBuffers();
        // All variables shared in the public interface to this class
        GLFWwindow *window;
        wgpu::Device device;
        wgpu::Queue queue;
        wgpu::Surface surface;
		std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
		wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
		wgpu::RenderPipeline pipeline;
		wgpu::Buffer pointBuffer;
		wgpu::Buffer indexBuffer;
		uint32_t indexCount;
};

int main(int, char**){
    Application app;

    if(!app.Initialize()){
        return 1;
    }

#ifdef __EMSCRIPTEN__
//Equivalent of the main loop when using emscripten
    auto callback = [](void * arg){
        Application * pApp = reinterpret_cast<Application*>(arg);
        pApp->MainLoop();
    };
    emscripten_set_main_loop_arg(callback, &app,0,true);
#else
    while(app.IsRunning()){
        app.MainLoop();
    }
#endif

    return 0;
}

bool Application::Initialize(){
	// Open window
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);
	
	wgpu::Instance instance = wgpuCreateInstance(nullptr);
	
	//Get adapter
	std::cout << "Requesting adapter..." << std::endl;
	surface = glfwGetWGPUSurface(instance, window);
	wgpu::RequestAdapterOptions adapterOpts = {};
	adapterOpts.compatibleSurface = surface;
	wgpu::Adapter adapter = instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;
	
	instance.release();
	
	std::cout << "Requesting device..." << std::endl;
	wgpu::DeviceDescriptor deviceDesc = {};
	deviceDesc.label = "My Device";
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = "The default queue";
	deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	};
	//Before adapter.requestDevice(deviceDesc)
	wgpu::RequiredLimits requiredLimits = GetRequiredLimits(adapter);
	deviceDesc.requiredLimits = &requiredLimits;
	std::cout << "Got the required limits:" << std::endl;
	std::cout << "maxInterStageShaderComponents: " << deviceDesc.requiredLimits->limits.maxInterStageShaderComponents << std::endl;
	device = adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << device << std::endl;
	
	//Device error callback
	uncapturedErrorCallbackHandle = device.setUncapturedErrorCallback([](wgpu::ErrorType type, char const* message){
		std::cout<<"Uncaptured device error: type "<< type;
		if(message)std::cout<<"("<<message<<")";
		std::cout<<std::endl;
	});
	
	queue = device.getQueue();

	// Configure the surface
	wgpu::SurfaceConfiguration config = {};

	// Configuration of the textures created for the underlying swap chain
	config.width = 640;
	config.height = 480;
	config.usage = wgpu::TextureUsage::RenderAttachment;
	surfaceFormat = surface.getPreferredFormat(adapter);
	config.format = surfaceFormat;

	// And we do not need any particular view format:
	config.viewFormatCount = 0;
	config.viewFormats = nullptr;
	config.device = device;
	config.presentMode = wgpu::PresentMode::Fifo;
	config.alphaMode = wgpu::CompositeAlphaMode::Auto;

	surface.configure(config);

	// Release the adapter only after it has been fully utilized
	adapter.release();

	InitializePipeline();
	InitializeBuffers();
	return true;
}

void Application::Terminate(){
	pointBuffer.release();
	indexBuffer.release();
	pipeline.release();
	surface.unconfigure();
	queue.release();
	surface.release();
	device.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop(){
	glfwPollEvents();

	// Get the next target texture view
	wgpu::TextureView targetView = GetNextSurfaceTextureView();
	if (!targetView) return;

	// Create a command encoder for the draw call
	wgpu::CommandEncoderDescriptor encoderDesc = {};
	encoderDesc.label = "My command encoder";
	wgpu::CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	// Create the render pass that clears the screen with our color
	wgpu::RenderPassDescriptor renderPassDesc = {};

	// The attachment part of the render pass descriptor describes the target texture of the pass
	wgpu::RenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = wgpu::LoadOp::Clear;
	renderPassColorAttachment.storeOp = wgpu::StoreOp::Store;
	renderPassColorAttachment.clearValue = WGPUColor{ 0.05, 0.05, 0.05, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;
	renderPassDesc.depthStencilAttachment = nullptr;
	renderPassDesc.timestampWrites = nullptr;

	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	//Select which render pipeline to use
	renderPass.setPipeline(pipeline);

	//Set POINT buffer while encoding the render pass
	renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());

	//The second argument must correspond to the choice of uint16_t or uint32_t
	//we are done with creating the index buffer
	renderPass.setIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint16, 0, indexBuffer.getSize());

	//Replace 'draw()' with 'drawIndexed()' and 'vertexCount' with 'indexCount'
	//The extra argument is an offset within the index buffer.
	renderPass.drawIndexed(indexCount, 1, 0, 0, 0);

	renderPass.end();
	renderPass.release();

	// Finally encode and submit the render pass
	wgpu::CommandBufferDescriptor cmdBufferDescriptor = {};
	cmdBufferDescriptor.label = "Command buffer";
	wgpu::CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	encoder.release();

	std::cout << "Submitting command..." << std::endl;
	queue.submit(1, &command);
	command.release();
	std::cout << "Command submitted." << std::endl;

	std::cout << "Surface format: "<< surfaceFormat << std::endl;

	// At the end of the frame
	targetView.release();
#ifndef __EMSCRIPTEN__
	surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
	device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
	device.poll(false);
#endif
};

bool Application::IsRunning(){
    return !glfwWindowShouldClose(window);
};

wgpu::TextureView Application::GetNextSurfaceTextureView(){
    // Get the surface texture
	wgpu::SurfaceTexture surfaceTexture;
	surface.getCurrentTexture(&surfaceTexture);
	if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success) {
		return nullptr;
	}
	wgpu::Texture texture = surfaceTexture.texture;

	// Create a view for this surface texture
	wgpu::TextureViewDescriptor viewDescriptor;
	viewDescriptor.label = "Surface texture view";
	viewDescriptor.format = texture.getFormat();
	viewDescriptor.dimension = wgpu::TextureViewDimension::_2D;
	viewDescriptor.baseMipLevel = 0;
	viewDescriptor.mipLevelCount = 1;
	viewDescriptor.baseArrayLayer = 0;
	viewDescriptor.arrayLayerCount = 1;
	viewDescriptor.aspect = wgpu::TextureAspect::All;
	wgpu::TextureView targetView = texture.createView(viewDescriptor);

	return targetView;
}

void Application::InitializePipeline(){
	std::cout << "Creating shader module..." << std::endl;
	wgpu::ShaderModule shaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", device);
	std::cout << "Shader module: " << shaderModule << std::endl;

	// Check for errors
	if (shaderModule == nullptr) {
		std::cerr << "Could not load shader!" << std::endl;
		exit(1);
	}
	//Create the render pipeline
	wgpu::RenderPipelineDescriptor pipelineDesc;

	//Configure the vertex pipeline
	//We use one vertex buffer
	wgpu::VertexBufferLayout vertexBufferLayout;
	std::vector<wgpu::VertexAttribute> vertexAttribs(2);

	//Describe the position attribute
	vertexAttribs[0].shaderLocation = 0;//@location(0)
	vertexAttribs[0].format = wgpu::VertexFormat::Float32x2;
	vertexAttribs[0].offset=0;

	//Describe the color attribute
	vertexAttribs[1].shaderLocation = 1;//@location(1)
	vertexAttribs[1].format = wgpu::VertexFormat::Float32x3;//different type!
	vertexAttribs[1].offset = 2*sizeof(float);//non null offset!

	vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
	vertexBufferLayout.attributes = vertexAttribs.data();

	vertexBufferLayout.arrayStride = 5*sizeof(float);
	//								^^^^^^^^^^^^^^^^ new stride
	vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;

	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;

	//Defined the 'shaderModule' in the second part of this chapter
	// Here we tell that the programmable vertex shader stage is described
	// by the function called 'vs_main' in that module.
	pipelineDesc.vertex.module = shaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	//Each sequence of 3 vertices is considered as a triangle
	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;

	//We'll see later how to specify the order in which vertices should be
	// connected. When not specified, vertices are considered sequentially.
	pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;

	// The face orientation is defined by assuming that when looking
	//from the front of the face, its corner vertices are enumerated
	// in the counter-clockwise (CCW) order.
	pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;

	//But the face orientation does not matter much because we do not cull
	// (i.e. 'hide') the faces pointing away from us (this is often used
	// for optimization).
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

	//We tell that the programmable fragment shader stage is described
	// by the function called 'fs_main' in the shader module.
	wgpu::FragmentState fragmentState;
	fragmentState.module = shaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;

	wgpu::BlendState blendState;
	blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = wgpu::BlendOperation::Add;
	blendState.alpha.srcFactor = wgpu::BlendFactor::Zero;
	blendState.alpha.dstFactor = wgpu::BlendFactor::One;
	blendState.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState colorTarget;
	colorTarget.format = surfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;//We could write to only some of the color channels.

	//We have only one target because our render pass has only one ouput color
	// attachment.
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;

	// We do not use stencil/depth testing for now
	pipelineDesc.depthStencil = nullptr;

	//Samples per pixel
	pipelineDesc.multisample.count = 1;

	//Default value for the mask, meaning "all bits on"
	pipelineDesc.multisample.mask = ~0u;

	// Default value as well(not relevant for count=1)
	pipelineDesc.multisample.alphaToCoverageEnabled = false;
	pipelineDesc.layout = nullptr;

	pipeline = device.createRenderPipeline(pipelineDesc);

	//We no longer need to access the shader module
	shaderModule.release();
}

wgpu::RequiredLimits Application::GetRequiredLimits(wgpu::Adapter adapter) const{
	//Get adapter supported limits, in case we need them
	wgpu::SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);

	//Don't forget to = Default
	wgpu::RequiredLimits requiredLimits = wgpu::Default;

	//We use at most 1 vertex attribute for now
	requiredLimits.limits.maxVertexAttributes = 2;
	//We should also tell that we use 1 vertex buffers
	requiredLimits.limits.maxVertexBuffers = 1;
	//Maximum size of a buffer is 6 vertices of 2 float each
	requiredLimits.limits.maxBufferSize = 15*5*sizeof(float);
	//										^ This value was a 6
	//Maximum stride between 2 consecutive vertices in the vertex buffer
	requiredLimits.limits.maxVertexBufferArrayStride = 5*sizeof(float);
	//												   ^ This was a 2

	// There is a maximum of 3 float forwarded from vertex to fragment shader
	requiredLimits.limits.maxInterStageShaderComponents = 3;

	// These two limits are different because they are "minimum" limits,
	// they are the only ones we may forward from the adapter's supported
	// limits.
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

	return requiredLimits;
}

void Application::InitializeBuffers(){
	//Vertex buffer data
	//There are 2 floats per vertex, one for x and one for y.
	std::vector<float> pointData;
	//Define index data
	// This is a list of indices referencing positions in the pointData
	std::vector<uint16_t> indexData;

	// Here we use the new 'loadGeometry' function:
	bool success = ResourceManager::loadGeometry(RESOURCE_DIR "/webgpu.txt", pointData, indexData);

	// Check for errors
	if (!success) {
		std::cerr << "Could not load geometry!" << std::endl;
		exit(1);
	}

	indexCount = static_cast<uint32_t>(indexData.size());

	//Create a POINT buffer
	wgpu::BufferDescriptor bufferDesc;
	bufferDesc.size = pointData.size()*sizeof(float);
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;// Vertex usage here!
	bufferDesc.mappedAtCreation = false;
	pointBuffer = device.createBuffer(bufferDesc);

	//Upload geometry data to the buffer
	queue.writeBuffer(pointBuffer,0,pointData.data(),bufferDesc.size);

	//Create index buffer
	//(we reuse the bufferDesc initialized for the pointBuffer)
	bufferDesc.size = indexData.size()*sizeof(uint16_t);
	bufferDesc.size = (bufferDesc.size + 3) & ~3;//round up to the nearest multiple of 4
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
	indexBuffer = device.createBuffer(bufferDesc);

	queue.writeBuffer(indexBuffer, 0 , indexData.data(), bufferDesc.size);
}
