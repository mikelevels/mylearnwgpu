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
#include <cstddef>// for offsetof
#include <cmath>// for std::pow
#include <array>// for std::array (Step043)
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
		//Substep of Initialize() that connects the uniform buffer to the pipeline
		void InitializeBindGroups();
        // All variables shared in the public interface to this class
        GLFWwindow *window = nullptr;
        wgpu::Device device = nullptr;
        wgpu::Queue queue = nullptr;
        wgpu::Surface surface = nullptr;
		std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
		wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
		wgpu::RenderPipeline pipeline = nullptr;
		wgpu::Buffer pointBuffer = nullptr;
		wgpu::Buffer indexBuffer = nullptr;
		uint32_t indexCount = 0;

		// Uniforms (Step039): values that are the same for every vertex and
		// fragment of one draw call, but that we may change between frames.
		wgpu::Buffer uniformBuffer = nullptr;
		// The bind group LAYOUT says what kind of resources the pipeline expects
		// (here: one uniform buffer at @group(0) @binding(0))...
		wgpu::BindGroupLayout bindGroupLayout = nullptr;
		// ...the pipeline LAYOUT is the list of all bind group layouts...
		wgpu::PipelineLayout pipelineLayout = nullptr;
		// ...and the BIND GROUP is the actual buffer plugged into that slot.
		wgpu::BindGroup bindGroup = nullptr;

		// 2.2 if the surface does sRGB conversion, 1.0 otherwise (set in
		// InitializeBuffers). Used for the shader AND for the clear color.
		float gamma = 1.0f;
};

/**
 * The C++ mirror of the `MyUniforms` struct declared in shader.wgsl.
 * The two MUST have the same memory layout, field for field, because we copy
 * this struct byte-for-byte into the uniform buffer.
 *
 * Memory layout rules (Step043, "More uniforms"), from the WGSL spec's
 * "address space layout constraints":
 *  1. ALIGNMENT: each field must start at an offset that is a multiple of its
 *     alignment. A vec4f has an alignment of 16 bytes, an f32 of 4 bytes.
 *     If `time` came first, `color` would start at offset 4, which is NOT a
 *     multiple of 16 and would be invalid. So the big field goes first:
 *        offset  0: color (16 bytes)
 *        offset 16: time  ( 4 bytes)
 *        offset 20: gamma ( 4 bytes)
 *        offset 24: _pad  ( 8 bytes)
 *  2. SIZE: the whole struct's size must be a multiple of its largest
 *     alignment (16 here), so we pad from 24 up to 32 bytes.
 * The WGSL side does not declare the padding: WGSL adds it implicitly.
 */
struct MyUniforms {
	std::array<float, 4> color; // RGBA tint applied to every fragment (vec4f in WGSL)
	float time;      // Seconds since the app started (glfwGetTime())
	float gamma;     // 2.2 when the surface is sRGB, 1.0 otherwise (see shader)
	float _pad[2];   // Unused, only here to reach 32 bytes (a multiple of 16)
};
// Have the compiler double check that we got the size right
static_assert(sizeof(MyUniforms) % 16 == 0, "MyUniforms must be a multiple of 16 bytes");
// ...and that color really sits where WGSL expects it (offset 0, then 16 for time)
static_assert(offsetof(MyUniforms, time) == 16, "time must start right after the 16-byte color");

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
	// Release every GPU object and close the window once the loop ends.
	// (In the browser the loop above never "ends": the page simply closes,
	// so there is no equivalent call in the __EMSCRIPTEN__ branch.)
	app.Terminate();
#endif

    return 0;
}

bool Application::Initialize(){
	// Open window
	// Every step below can fail (no GPU, driver too old, browser without
	// WebGPU...). We check each result right away and stop with a clear
	// message, instead of carrying on and crashing somewhere confusing later.
	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return false;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);
	if (!window) {
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}

	wgpu::Instance instance = wgpuCreateInstance(nullptr);

	//Get adapter
	std::cout << "Requesting adapter..." << std::endl;
	surface = glfwGetWGPUSurface(instance, window);
	wgpu::RequestAdapterOptions adapterOpts = {};
	adapterOpts.compatibleSurface = surface;
	wgpu::Adapter adapter = instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;
	if (!adapter) {
		std::cerr << "Could not get a WebGPU adapter!" << std::endl;
		return false;
	}

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
	std::cout << "maxVertexAttributes: " << deviceDesc.requiredLimits->limits.maxVertexAttributes << std::endl;
	device = adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << device << std::endl;
	if (!device) {
		// This is exactly what used to happen in the browser: Chrome refused
		// one of our required limits, so no device was created.
		std::cerr << "Could not get a WebGPU device!" << std::endl;
		return false;
	}

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
	InitializeBindGroups();
	return true;
}

void Application::Terminate(){
	// Release in the reverse order of creation: things that USE an object are
	// released before the object they use.
	// (WebGPU objects are reference counted, so a different order would not
	// crash, but this order is easy to reason about.)
	bindGroup.release();
	uniformBuffer.release();
	indexBuffer.release();
	pointBuffer.release();
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
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

	// Update the uniform buffer with the current time.
	// We only overwrite the `time` field: `offsetof` gives where it sits inside
	// MyUniforms, so the other fields (gamma) keep the value set at startup.
	float t = static_cast<float>(glfwGetTime());
	queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, time), &t, sizeof(float));

	// Create a command encoder for the draw call
	wgpu::CommandEncoderDescriptor encoderDesc = {};
	encoderDesc.label = "My command encoder";
	wgpu::CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

	// Create the render pass that clears the screen with our color
	wgpu::RenderPassDescriptor renderPassDesc = {};

	// The attachment part of the render pass descriptor describes the target texture of the pass
	wgpu::RenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = wgpu::LoadOp::Clear;
	renderPassColorAttachment.storeOp = wgpu::StoreOp::Store;
	// The clear color goes through the same sRGB conversion as our shader's
	// output, so it needs the same gamma correction to look identical on
	// every backend (otherwise it is near-black in the browser and grey with
	// wgpu-native).
	double background = std::pow(0.05, gamma);
	renderPassColorAttachment.clearValue = WGPUColor{ background, background, background, 1.0 };
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

	//Plug our bind group into slot @group(0) of the shader, so that uMyUniforms
	//reads from uniformBuffer. (The last two arguments are for "dynamic
	//offsets", which we do not use.)
	renderPass.setBindGroup(0, bindGroup, 0, nullptr);

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

	// No logging here: this function runs ~60 times per second, and printing
	// every frame floods the console and slows the app down.
	queue.submit(1, &command);
	command.release();

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

	// Describe the resources the shader expects (Step039).
	// Until now we let WebGPU guess the layout (layout = nullptr). From now on
	// we declare it explicitly, so that the bind group we create later is
	// guaranteed to match it.
	//
	// One binding: a uniform buffer at @binding(0), visible to both shader
	// stages (the vertex shader reads `time`, the fragment shader `gamma`).
	wgpu::BindGroupLayoutEntry bindingLayout = wgpu::Default;
	bindingLayout.binding = 0;// @binding(0)
	bindingLayout.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	bindingLayout.buffer.type = wgpu::BufferBindingType::Uniform;
	bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

	// A bind group layout is a list of binding layouts (we have only one)
	wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = 1;
	bindGroupLayoutDesc.entries = &bindingLayout;
	bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

	// A pipeline layout is a list of bind group layouts: index 0 in this list
	// is @group(0) in the shader.
	wgpu::PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&bindGroupLayout;
	pipelineLayout = device.createPipelineLayout(layoutDesc);

	pipelineDesc.layout = pipelineLayout;

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
	// Chrome has since removed this limit from the WebGPU standard and now
	// REFUSES to create a device if we set it. This is what broke the browser
	// build at Step037. Desktop backends (wgpu-native v0.19, Dawn 6536) are
	// older than that change and still know it, so we only skip it in the browser.
#ifndef __EMSCRIPTEN__
	requiredLimits.limits.maxInterStageShaderComponents = 3;
#endif // NOT __EMSCRIPTEN__

	// We use at most 1 bind group for now
	requiredLimits.limits.maxBindGroups = 1;
	// Use at most 1 uniform buffer per stage
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
	// Uniform structs have a size of maximum 16 float (more than what we need)
	requiredLimits.limits.maxUniformBufferBindingSize = 16*4;

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

	// writeBuffer only accepts sizes that are a multiple of 4 bytes, but each
	// index is 2 bytes. With an odd number of indices (15 in webgpu.txt) we
	// would copy 2 bytes past the end of indexData, i.e. read memory that does
	// not belong to us. Adding a dummy 0 index makes the vector itself long
	// enough. It is never drawn, because drawIndexed uses indexCount (saved
	// above, before the padding).
	if (indexData.size() % 2 != 0) {
		indexData.push_back(0);
	}

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
	bufferDesc.size = indexData.size()*sizeof(uint16_t);// already a multiple of 4, see padding above
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
	indexBuffer = device.createBuffer(bufferDesc);

	queue.writeBuffer(indexBuffer, 0 , indexData.data(), bufferDesc.size);

	//Create uniform buffer (reusing bufferDesc from the other buffers)
	//It holds exactly one MyUniforms struct.
	bufferDesc.size = sizeof(MyUniforms);
	//Make sure to flag the buffer as BufferUsage::Uniform
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	uniformBuffer = device.createBuffer(bufferDesc);

	//Upload the initial uniform values
	MyUniforms uniforms = {};
	uniforms.time = 0.0f;
	// A green tint, as in the guide (RGBA, each between 0 and 1). The shader
	// multiplies every vertex color by it, so blue logo * green tint = teal.
	uniforms.color = { 0.0f, 1.0f, 0.4f, 1.0f };
	// Gamma correction. Our colors in webgpu.txt are written in sRGB (the
	// space color pickers use). If the surface is an *sRGB* format, the GPU
	// converts linear -> sRGB when writing pixels, so we must first convert
	// our sRGB colors to linear (pow 2.2) or they come out washed out.
	// If the surface is a plain (non-sRGB) format, nothing is converted and we
	// must leave the colors alone (pow 1.0). Which one we get depends on the
	// backend: wgpu-native usually picks BGRA8UnormSrgb, Dawn and browsers
	// usually BGRA8Unorm. This is why your Step025 note saw different colors.
	bool isSrgb = surfaceFormat == wgpu::TextureFormat::BGRA8UnormSrgb
		|| surfaceFormat == wgpu::TextureFormat::RGBA8UnormSrgb;
	gamma = isSrgb ? 2.2f : 1.0f;
	uniforms.gamma = gamma;
	std::cout << "Surface format: " << surfaceFormat << " (sRGB: " << (isSrgb ? "yes" : "no") << ")" << std::endl;
	queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));
}

void Application::InitializeBindGroups(){
	// The bind group is where we say WHICH buffer goes into each binding
	// declared by bindGroupLayout. One entry per binding.
	wgpu::BindGroupEntry binding{};
	binding.binding = 0;// Must match bindingLayout.binding and @binding(0)
	binding.buffer = uniformBuffer;
	binding.offset = 0;// We use the buffer from its very beginning...
	binding.size = sizeof(MyUniforms);// ...up to the size of our struct

	wgpu::BindGroupDescriptor bindGroupDesc{};
	bindGroupDesc.layout = bindGroupLayout;// The bind group must follow this layout
	bindGroupDesc.entryCount = 1;
	bindGroupDesc.entries = &binding;
	bindGroup = device.createBindGroup(bindGroupDesc);
}
