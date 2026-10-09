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
#include <memory>// for std::unique_ptr (it only arrived through webgpu.hpp before)
#include <utility>// for std::pair

// GLM ("OpenGL Mathematics", Step055): vectors and matrices on the C++ side,
// with the same names and layout as in shaders (vec3, vec4, mat4x4...).
// It is header-only: the glm/ folder at the root of the project is all of it.
// The two defines MUST come before the include:
// - DEPTH_ZERO_TO_ONE: WebGPU keeps depths between 0 and 1 (OpenGL, which
//   GLM was made for, uses -1 to 1).
// - LEFT_HANDED: x to the right, y up, z going INTO the screen, like WebGPU's
//   clip space. (OpenGL's z comes out of the screen.)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp> // all types inspired from GLSL
#include <glm/ext.hpp> // glm::perspective, glm::rotate, glm::translate...

using glm::mat4x4;
using glm::vec4;
using glm::vec3;

#include "ResourceManager.h"

constexpr float PI = 3.14159265358979323846f;

/**
 * The C++ mirror of the `MyUniforms` struct declared in shader.wgsl.
 * The two MUST have the same memory layout, field for field, because we copy
 * this struct byte-for-byte into the uniform buffer.
 *
 * Memory layout rules (Step043, "More uniforms"), from the WGSL spec's
 * "address space layout constraints":
 *  1. ALIGNMENT: each field must start at an offset that is a multiple of its
 *     alignment. A mat4x4f and a vec4f have an alignment of 16 bytes, an f32
 *     of 4 bytes. So the big fields go first:
 *        offset   0: projectionMatrix (64 bytes)   <- Step055
 *        offset  64: viewMatrix       (64 bytes)   <- Step055
 *        offset 128: modelMatrix      (64 bytes)   <- Step055
 *        offset 192: color            (16 bytes)
 *        offset 208: time             ( 4 bytes)
 *        offset 212: gamma            ( 4 bytes)
 *        offset 216: _pad             ( 8 bytes)
 *  2. SIZE: the whole struct's size must be a multiple of its largest
 *     alignment (16 here), so we pad from 216 up to 224 bytes.
 * The WGSL side does not declare the padding: WGSL adds it implicitly.
 *
 * glm::mat4x4 stores its 16 floats column by column, exactly like WGSL's
 * mat4x4f, so it can be copied as is.
 */
struct MyUniforms {
	mat4x4 projectionMatrix; // camera space -> clip space (perspective)
	mat4x4 viewMatrix;       // world space -> camera space (where we look from)
	mat4x4 modelMatrix;      // model space -> world space (where the object is)
	std::array<float, 4> color; // RGBA tint applied to every fragment (vec4f in WGSL)
	float time;      // Seconds since the app started (glfwGetTime())
	float gamma;     // 2.2 when the surface is sRGB, 1.0 otherwise (see shader)
	float _pad[2];   // Unused, only here to reach 224 bytes (a multiple of 16)
};
// Have the compiler double check that we got the size right...
static_assert(sizeof(MyUniforms) % 16 == 0, "MyUniforms must be a multiple of 16 bytes");
// ...and that the fields really sit where WGSL expects them
static_assert(offsetof(MyUniforms, color) == 192, "color must start right after the 3 matrices");
static_assert(offsetof(MyUniforms, time) == 208, "time must start right after the 16-byte color");

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
		// Get the texture to draw into this frame and a view of it. The
		// texture is returned too because it must be released (see the .cpp).
		std::pair<wgpu::SurfaceTexture, wgpu::TextureView> GetNextSurfaceViewData();
		//Substep of Initialize() that creates the render pipeline
		//(returns false if the shader could not be loaded)
		bool InitializePipeline();
		wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;
		//Substep of Initialize() that loads the geometry and creates the buffers
		//(returns false if the geometry file could not be loaded)
		bool InitializeBuffers();
		//Substep of Initialize() that connects the uniform buffer to the pipeline
		void InitializeBindGroups();
		//Substep of Initialize() that creates the depth texture (Step052)
		void InitializeDepthBuffer();
        // All the state shared by the methods of this class (private: only
        // the Application itself can touch it)
        GLFWwindow *window = nullptr;
		// The instance is KEPT for the whole life of the app. Releasing it
		// right after getting the adapter (as we used to) shuts down Dawn's
		// event system, so callbacks such as "device lost" could never fire.
		wgpu::Instance instance = nullptr;
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
		// Step050: the dynamic uniforms of Step044 (two logos from one buffer)
		// are rolled back, like in the guide, to keep the 3D code simple.
		// That version is still in git history: commit f446f3e.

		// Depth buffer (Step052), a.k.a. Z-buffer: a texture the size of the
		// window that stores, for each pixel, the depth of the closest
		// fragment drawn so far. The pipeline needs its FORMAT, the render
		// pass needs a VIEW of it.
		wgpu::TextureFormat depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
		wgpu::Texture depthTexture = nullptr;
		wgpu::TextureView depthTextureView = nullptr;

		// Step055: the CPU copy of the uniforms, kept so MainLoop() can update
		// the model matrix each frame. The pieces of the model matrix that do
		// not change over time (scale and translation) are kept as well.
		MyUniforms uniforms = {};
		mat4x4 modelScale = mat4x4(1.0);       // S in the guide
		mat4x4 modelTranslation = mat4x4(1.0); // T1 in the guide
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

	instance = wgpuCreateInstance(nullptr);
	if (!instance) {
		std::cerr << "Could not create a WebGPU instance!" << std::endl;
		return false;
	}

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

	// (No instance.release() here anymore: see the `instance` member.)

	std::cout << "Requesting device..." << std::endl;
	wgpu::DeviceDescriptor deviceDesc = {};
	deviceDesc.label = "My Device";
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = "The default queue";
	// NOTE: now that the instance stays alive, Dawn can deliver this callback.
	// It also fires when we release everything on exit: with Dawn you will
	// see "Device lost: reason 3 (A valid external Instance reference no
	// longer exists.)" when you close the window. That is EXPECTED.
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

	// These substeps used to call exit(1) on failure, which skipped all
	// cleanup. They now report failure and we pass it up to main().
	if (!InitializePipeline()) return false;
	InitializeDepthBuffer();
	if (!InitializeBuffers()) return false;
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
	// Step052: the view first, then the texture. destroy() frees the GPU
	// memory right away; release() drops our handle to the object.
	depthTextureView.release();
	depthTexture.destroy();
	depthTexture.release();
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
	surface.unconfigure();
	queue.release();
	surface.release();
	device.release();
	// The instance goes last: everything above was created through it.
	instance.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop(){
	glfwPollEvents();

	// Get the next target texture view (and the texture itself, which we
	// must release at the right moment, see GetNextSurfaceViewData)
	auto [surfaceTexture, targetView] = GetNextSurfaceViewData();
	if (!targetView) return;

	// Update the uniform buffer with the current time.
	// We only overwrite the fields that change: `offsetof` gives where each
	// one sits inside MyUniforms, so the other fields keep their values.
	uniforms.time = static_cast<float>(glfwGetTime());
	queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, time), &uniforms.time, sizeof(MyUniforms::time));

	// Step055: the spin of the pyramid is now done on the CPU. We rebuild the
	// model matrix with the new angle (rotation around Z) and upload ONLY
	// that matrix (64 bytes). Read right to left: scale, translate, rotate.
	float angle1 = uniforms.time;
	mat4x4 R1 = glm::rotate(mat4x4(1.0), angle1, vec3(0.0, 0.0, 1.0));
	uniforms.modelMatrix = R1 * modelTranslation * modelScale;
	queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, modelMatrix), &uniforms.modelMatrix, sizeof(MyUniforms::modelMatrix));

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

	// Step052: we now add a depth/stencil attachment, next to the color one.
	wgpu::RenderPassDepthStencilAttachment depthStencilAttachment;
	// The view of the depth texture created in InitializeDepthBuffer()
	depthStencilAttachment.view = depthTextureView;
	// The initial value of the depth buffer: 1.0 means "as far as possible",
	// so the first fragment drawn on each pixel always passes the test.
	depthStencilAttachment.depthClearValue = 1.0f;
	// Same idea as the color attachment: clear at the start of the pass,
	// keep the result at the end.
	depthStencilAttachment.depthLoadOp = wgpu::LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = wgpu::StoreOp::Store;
	// We could turn off writing to the depth buffer for the whole pass here
	depthStencilAttachment.depthReadOnly = false;

	// Stencil setup: mandatory fields, but we do not use a stencil.
	// Depth24Plus has NO stencil part, so the stencil ops must be Undefined
	// (Dawn and Chrome reject anything else).
	// The guide wraps this in #ifdef WEBGPU_BACKEND_WGPU and uses Clear/Store
	// for wgpu-native. We tested Undefined with OUR wgpu-native (v0.19): no
	// error and the same picture, so one version works for all three builds
	// and we avoid an #ifdef.
	depthStencilAttachment.stencilClearValue = 0;
	depthStencilAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
	depthStencilAttachment.stencilReadOnly = true;

	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
	renderPassDesc.timestampWrites = nullptr;

	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	//Select which render pipeline to use
	renderPass.setPipeline(pipeline);

	//Set POINT buffer while encoding the render pass
	renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());

	//The second argument must match the type of our index data: uint16_t
	//here (Uint16), it would be Uint32 for a std::vector<uint32_t>
	renderPass.setIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint16, 0, indexBuffer.getSize());

	//Plug our bind group into slot @group(0) of the shader, so that uMyUniforms
	//reads from uniformBuffer. (Back to no dynamic offsets: 0, nullptr.)
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

#ifdef WEBGPU_BACKEND_WGPU
	// wgpu-native only: the surface texture must be released AFTER present().
	// Releasing it earlier makes wgpu-native throw the frame away. (Dawn and
	// the browser already released it in GetNextSurfaceViewData.)
	wgpuTextureRelease(surfaceTexture.texture);
#endif // WEBGPU_BACKEND_WGPU

#if defined(WEBGPU_BACKEND_DAWN)
	device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
	device.poll(false);
#endif
};

bool Application::IsRunning(){
    return !glfwWindowShouldClose(window);
};

std::pair<wgpu::SurfaceTexture, wgpu::TextureView> Application::GetNextSurfaceViewData(){
    // Get the surface texture
	wgpu::SurfaceTexture surfaceTexture;
	surface.getCurrentTexture(&surfaceTexture);
	if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success) {
		// Even on failure we may have been handed a texture: give it back.
		if (surfaceTexture.texture) {
			wgpuTextureRelease(surfaceTexture.texture);
		}
		return { surfaceTexture, nullptr };
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

	// BUG FIX (found by the design review, measured on Dawn): every call to
	// getCurrentTexture() hands us a NEW reference to the texture, and we
	// never gave it back. Dawn's memory grew by about 70 KB per second.
	// The view keeps its own reference, so we can release ours right away...
#ifndef WEBGPU_BACKEND_WGPU
	wgpuTextureRelease(surfaceTexture.texture);
#endif // NOT WEBGPU_BACKEND_WGPU
	// ...except on wgpu-native, where it must wait until after present()
	// (done at the end of MainLoop). This is how the guide's current
	// "First Color" chapter does it; our older copy of that chapter did not.

	return { surfaceTexture, targetView };
}

bool Application::InitializePipeline(){
	std::cout << "Creating shader module..." << std::endl;
	wgpu::ShaderModule shaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", device);
	std::cout << "Shader module: " << shaderModule << std::endl;

	// Check for errors (only catches a MISSING file: a typo inside the WGSL is
	// reported later by the uncaptured error callback)
	if (shaderModule == nullptr) {
		std::cerr << "Could not load shader!" << std::endl;
		return false;
	}
	//Create the render pipeline
	wgpu::RenderPipelineDescriptor pipelineDesc;

	//Configure the vertex pipeline
	//We use one vertex buffer
	wgpu::VertexBufferLayout vertexBufferLayout;
	std::vector<wgpu::VertexAttribute> vertexAttribs(2);

	//Describe the position attribute
	vertexAttribs[0].shaderLocation = 0;//@location(0)
	vertexAttribs[0].format = wgpu::VertexFormat::Float32x3;
	//												  ^ This was a 2 (Step050: x, y AND z)
	vertexAttribs[0].offset=0;

	//Describe the color attribute
	vertexAttribs[1].shaderLocation = 1;//@location(1)
	vertexAttribs[1].format = wgpu::VertexFormat::Float32x3;//different type!
	vertexAttribs[1].offset = 3*sizeof(float);//non null offset!
	//						  ^ This was a 2: the color now starts after x, y, z

	vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
	vertexBufferLayout.attributes = vertexAttribs.data();

	vertexBufferLayout.arrayStride = 6*sizeof(float);
	//								^ This was a 5: x y z r g b
	vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;

	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;

	//The 'shaderModule' was loaded at the top of this function
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

	// Step052: depth testing. For each fragment, the GPU compares its depth
	// with the one already stored in the depth buffer for that pixel.
	wgpu::DepthStencilState depthStencilState = wgpu::Default;
	// Keep a fragment only if its depth is LOWER (closer) than the stored one.
	// (The default is Always, which is the same as no depth test at all.)
	depthStencilState.depthCompare = wgpu::CompareFunction::Less;
	// Each time a fragment is kept, store its depth so later fragments are
	// compared against it.
	depthStencilState.depthWriteEnabled = true;
	// Must be the same format as the depth texture (see InitializeDepthBuffer)
	depthStencilState.format = depthTextureFormat;
	// Deactivate the stencil altogether
	depthStencilState.stencilReadMask = 0;
	depthStencilState.stencilWriteMask = 0;

	pipelineDesc.depthStencil = &depthStencilState;

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
	return true;
}

wgpu::RequiredLimits Application::GetRequiredLimits(wgpu::Adapter adapter) const{
	//Get adapter supported limits, in case we need them
	wgpu::SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);

	//Don't forget to = Default
	wgpu::RequiredLimits requiredLimits = wgpu::Default;

	//We use 2 vertex attributes: position and color
	requiredLimits.limits.maxVertexAttributes = 2;
	//...and 1 vertex buffer
	requiredLimits.limits.maxVertexBuffers = 1;
	//Maximum size of a buffer: the biggest of our buffers.
	// - the point buffer: 5 points of 6 floats each (pyramid.txt) = 120 bytes
	// - the index buffer: 18 indices of 2 bytes = 36 bytes
	// - the uniform buffer: one MyUniforms = 32 bytes
	// The guide keeps 15*5*sizeof(float) = 300 bytes from the logo, which is
	// more than enough. We keep it too, as headroom.
	requiredLimits.limits.maxBufferSize = 15*5*sizeof(float);
	//Maximum stride between 2 consecutive vertices in the vertex buffer
	requiredLimits.limits.maxVertexBufferArrayStride = 6*sizeof(float);
	//												   ^ This was a 5 (Step050: x y z r g b)

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
	// Step055: our uniform struct grew to 224 bytes (3 matrices of 64 bytes +
	// color + time + gamma + padding). The old limit of 16 floats (64 bytes)
	// would make the bind group invalid on wgpu-native, which applies the
	// limits we ask for EXACTLY (Dawn silently rounds small limits up to its
	// defaults, so it would not have complained: a backend difference!).
	// We ask for 16*4 floats = 256 bytes.
	requiredLimits.limits.maxUniformBufferBindingSize = 16*4*sizeof(float);

	// These two limits are different because they are "minimum" limits,
	// they are the only ones we may forward from the adapter's supported
	// limits.
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

	// Step052: for the depth buffer we now use a texture, up to the size of
	// the window (640 x 480). The guide sets the 1D limit to 480; we do not
	// use 1D textures, it is just "something small".
	requiredLimits.limits.maxTextureDimension1D = 480;
	requiredLimits.limits.maxTextureDimension2D = 640;
	requiredLimits.limits.maxTextureArrayLayers = 1;

	return requiredLimits;
}

bool Application::InitializeBuffers(){
	//Vertex buffer data
	//Step050: there are now 6 floats per vertex: x, y, z then r, g, b.
	std::vector<float> pointData;
	//Define index data
	// This is a list of indices referencing positions in the pointData
	std::vector<uint16_t> indexData;

	// Step050: load the 3D pyramid instead of the 2D logo. The last argument
	// tells loadGeometry how many position coordinates each line has.
	bool success = ResourceManager::loadGeometry(RESOURCE_DIR "/pyramid.txt", pointData, indexData, 3 /* dimensions */);

	// Check for errors
	if (!success) {
		std::cerr << "Could not load geometry!" << std::endl;
		return false;
	}

	indexCount = static_cast<uint32_t>(indexData.size());

	// writeBuffer only accepts sizes that are a multiple of 4 bytes, but each
	// index is 2 bytes. With an odd number of indices (15 in webgpu.txt, the
	// pyramid has 18 so it does not need it, but the next model might) we
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
	//It holds exactly one MyUniforms struct again (Step050 rolls back the
	//two blocks of Step044).
	bufferDesc.size = sizeof(MyUniforms);
	//Make sure to flag the buffer as BufferUsage::Uniform
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	uniformBuffer = device.createBuffer(bufferDesc);

	//Upload the initial uniform values
	//(`uniforms` is now a member: MainLoop() updates it every frame)
	uniforms = {};
	uniforms.time = 0.0f;
	// A green tint, as in the guide (RGBA, each between 0 and 1). The shader
	// multiplies every vertex color by it, so the white pyramid base looks green.
	uniforms.color = { 0.0f, 1.0f, 0.4f, 1.0f };

	// Step055: build the transform matrices ONCE here on the CPU, instead of
	// for every single vertex in the shader (that was Step054). The guide
	// shows three ways of building the same matrices; each one overwrites
	// the previous, Option C is the one that is used.
	//
	// The three matrices, applied right to left to each vertex:
	//   projection * view * model * position
	//   - model: where the object sits in the world (scale, move, spin)
	//   - view: where the camera is and where it looks
	//   - projection: how the 3D view is flattened onto the screen

	// --- Option A: write the matrices by hand, exactly like in Step054's
	// shader. glm::mat4x4 also takes its numbers column by column, hence the
	// same transpose() trick.
	// Scale the object
	mat4x4 S = glm::transpose(mat4x4(
		0.3, 0.0, 0.0, 0.0,
		0.0, 0.3, 0.0, 0.0,
		0.0, 0.0, 0.3, 0.0,
		0.0, 0.0, 0.0, 1.0
	));

	// Translate the object
	mat4x4 T1 = glm::transpose(mat4x4(
		1.0, 0.0, 0.0, 0.5,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	));

	// Translate the view: moving the CAMERA back to z = -2 is the same as
	// moving the whole world forward by +2, hence the minus signs.
	vec3 focalPoint(0.0, 0.0, -2.0);
	mat4x4 T2 = glm::transpose(mat4x4(
		1.0, 0.0, 0.0, -focalPoint.x,
		0.0, 1.0, 0.0, -focalPoint.y,
		0.0, 0.0, 1.0, -focalPoint.z,
		0.0, 0.0, 0.0, 1.0
	));

	// Rotate the object (MainLoop replaces this with the time every frame)
	float angle1 = 2.0f; // arbitrary time
	float c1 = std::cos(angle1);
	float s1 = std::sin(angle1);
	mat4x4 R1 = glm::transpose(mat4x4(
		 c1,  s1, 0.0, 0.0,
		-s1,  c1, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	));

	// Rotate the view point by three 8th of a turn
	float angle2 = 3.0f * PI / 4.0f;
	float c2 = std::cos(angle2);
	float s2 = std::sin(angle2);
	mat4x4 R2 = glm::transpose(mat4x4(
		1.0, 0.0, 0.0, 0.0,
		0.0,  c2,  s2, 0.0,
		0.0, -s2,  c2, 0.0,
		0.0, 0.0, 0.0, 1.0
	));

	uniforms.modelMatrix = R1 * T1 * S;
	uniforms.viewMatrix = T2 * R2;

	// The perspective projection, by hand. The key is the last row
	// (0, 0, 1/focalLength, 0): it makes w = z / focalLength, and the GPU
	// then DIVIDES x, y and z by w. Far things (big z) get divided more, so
	// they look SMALLER. That is perspective.
	// The third row maps z from [near, far] to the depth range [0, 1]
	// (replacing the "z * 0.5 + 0.5" trick of Step052).
	float ratio = 640.0f / 480.0f;
	float focalLength = 2.0;
	float near = 0.01f;
	float far = 100.0f;
	float divider = 1 / (focalLength * (far - near));
	uniforms.projectionMatrix = glm::transpose(mat4x4(
		1.0, 0.0, 0.0, 0.0,
		0.0, ratio, 0.0, 0.0,
		0.0, 0.0, far * divider, -far * near * divider,
		0.0, 0.0, 1.0 / focalLength, 0.0
	));

	// --- Option B: let GLM build each matrix for us.
	S = glm::scale(mat4x4(1.0), vec3(0.3f));
	T1 = glm::translate(mat4x4(1.0), vec3(0.5, 0.0, 0.0));
	R1 = glm::rotate(mat4x4(1.0), angle1, vec3(0.0, 0.0, 1.0));
	uniforms.modelMatrix = R1 * T1 * S;

	R2 = glm::rotate(mat4x4(1.0), -angle2, vec3(1.0, 0.0, 0.0));
	T2 = glm::translate(mat4x4(1.0), -focalPoint);
	uniforms.viewMatrix = T2 * R2;

	// --- Option C: chain GLM calls on one matrix. CAREFUL: each call
	// multiplies on the RIGHT, so the calls are written in the OPPOSITE order
	// of what happens to the vertex (rotate is written first but applied last).
	mat4x4 M(1.0);
	M = glm::rotate(M, angle1, vec3(0.0, 0.0, 1.0));
	M = glm::translate(M, vec3(0.5, 0.0, 0.0));
	M = glm::scale(M, vec3(0.3f));
	uniforms.modelMatrix = M;

	mat4x4 V(1.0);
	V = glm::translate(V, -focalPoint);
	V = glm::rotate(V, -angle2, vec3(1.0, 0.0, 0.0));
	uniforms.viewMatrix = V;

	// glm::perspective takes a vertical field of view instead of a focal
	// length: a focal length of 2 means seeing 1 unit up for 2 units forward.
	float fov = 2 * glm::atan(1 / focalLength);
	uniforms.projectionMatrix = glm::perspective(fov, ratio, near, far);

	// Keep the parts of the model matrix that never change, for MainLoop()
	modelScale = S;
	modelTranslation = T1;
	// Gamma correction. Our colors in the .txt files are written in sRGB (the
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
	return true;
}

void Application::InitializeDepthBuffer(){
	// Create the depth texture (Step052).
	// It must be exactly the size of the surface we draw to: 640 x 480.
	// (Hard-coded for now, like the window. It will have to be re-created
	// when we allow resizing, in "Resizing the window".)
	wgpu::TextureDescriptor depthTextureDesc;
	depthTextureDesc.dimension = wgpu::TextureDimension::_2D;
	depthTextureDesc.format = depthTextureFormat;
	depthTextureDesc.mipLevelCount = 1;
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.size = {640, 480, 1};
	// The only thing we do with it is use it as a render pass attachment
	depthTextureDesc.usage = wgpu::TextureUsage::RenderAttachment;
	depthTextureDesc.viewFormatCount = 1;
	depthTextureDesc.viewFormats = (WGPUTextureFormat*)&depthTextureFormat;
	depthTexture = device.createTexture(depthTextureDesc);
	std::cout << "Depth texture: " << depthTexture << std::endl;

	// Create the view of the depth texture used by the render pass.
	// A texture can hold many images (mip levels, array layers) and a view
	// says which part we use. Here: the whole texture, depth part only.
	wgpu::TextureViewDescriptor depthTextureViewDesc;
	depthTextureViewDesc.aspect = wgpu::TextureAspect::DepthOnly;
	depthTextureViewDesc.baseArrayLayer = 0;
	depthTextureViewDesc.arrayLayerCount = 1;
	depthTextureViewDesc.baseMipLevel = 0;
	depthTextureViewDesc.mipLevelCount = 1;
	depthTextureViewDesc.dimension = wgpu::TextureViewDimension::_2D;
	depthTextureViewDesc.format = depthTextureFormat;
	depthTextureView = depthTexture.createView(depthTextureViewDesc);
	std::cout << "Depth texture view: " << depthTextureView << std::endl;
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
