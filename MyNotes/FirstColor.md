
Step025

# First Color

The goal is to draw a solid color to the window. Add the following to the code:
1) We must first configure the surface of our window.
2) We then get at each frame the Surface Texture to draw onto.
3) And finally we create a render pass to effectively draw something.

## Surface Configuration

At the end of the previous chapter the surface object was introduced as the link between the OS window(GLFW handles this) and the WebGPU instance.

However, this surface needs to be configured before we can draw on it. Why? What follows is a high level overview of how the windows' surface is drawn.

### Drawing process

The render pipeline does not draw directly on the texture that is currently displayed, otherwise users would see pixels change all the time. Typical pipelines draw to an off-screen texture. The currently displayed texture is only replaced once the offscreen buffer is complete. Once the offscreen texture is displayed we say the texture is presented to the surface.

Drawing takes a different time than the frame rate required by your application, so the GPU may have to wait until the next frame is needed. There might be more than one off-screen texture waiting in the queue to be presented, so that fluctuations in the render time get amortized.

Off screen textures are typically reused as much as possible. As soon as a new texture is presented the previous one can be reused as a target for the next frame. The described mechanism is called the Swap Chain. This is handled under the hood by the Surface object.

> Note
> Recall the GPU process occurs at a different pace. CPU-issued commands are only asynchronously executed. The provided swap chain process is difficult to implement manually and would require a lot of boilerplate. Thankfully it is provided by the API.

### Configuration

The process described has a couple of parameters set through `wgpuSurfaceConfigure`:

```Cpp
WGPUSurfaceConfiguration config = {};
config.nextInChain = nullptr;

{{Describe Surface Configuration}}

wgpuSurfaceConfigure(surface, &config);
```

This must occur after initialization. At the end of the program, we may unconfigure the surface:

```Cpp
wgpuSurfaceUnconfigure(surface);
```

### Texture Parameters

First specify the parameters used to allocate the textures for the underlying swap chain. This includes the size, format, and usage.

```Cpp
// Configuration of the textures created for the underlying swap chain
config.width = 640;
config.height = 480;
{{Describe Surface Usage}}
{{Describe Surface Format}}
```

> Warning
>As you can see in the snippet above the surface must be reconfigured if  the window is resized. Do not try to resize the window for now. You currently have GLFW configured to disable resizing.

Texture format is a combination of a number of channels(a subset of red, green, blue, alpha) a size per channel(8,16, or 32 bits) and a channel type(float, integer, signed or not), a compression scheme, a normalization mode, etc.

All available combinations are listed in the `WGPUTextureFormat` enum, but since our swap chain targets an existing surface, we can just use whichever format the surface uses:

```Cpp
WGPUTextureFormat surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
config.format = surfaceFormat;
// And we do not need any particular view format:
config.viewFormatCount = 0;
config.viewFormats = nullptr;
```

>Warning
> Make sure to move the call to `wgpuAdapterRelease` after the call to `wgpuSurfaceGetPreferredFormat`, the latter uses our `adapter` handle.

Textures are allocated for a specific usage. This dictates te way the GPU organizes memory. In our case, the swap chain textures are used as targets for a Render Pass so it needs to be created with the `RenderAttachement` usage flag:

```Cpp
config.usage = WGPUTextureUsage_RenderAttachment;
```

Lastly, the surface needs to know the device to use to create the textures:

```Cpp
config.device = device;
```

### Presentation parameters

After telling how to allocate textures, we can tell which texture from the waiting queue must be presented at each frame. Possible values are found in the `WGPUPresentMode` enum:

* `Immediate`: No off-screen texture is used, the render process directly draws on the surface, which might lead to artifacts(called tearing) but has zero latency.
* `Mailbox`: There is only one slot in the queue, and when a new frame is rendered, it replaces the one currently waiting(which is discarded without ever being presented).
* `Fifo`: Stands for "first in, first out", meaning that the presented texture is always the oldest one, like a regular queue. No rendered texture is wasted.

> Tip
> The `Force32` enum values found in `webgpu.h` is not a legal value, it is just there to force the underlying enum type to be a 32-bit integer.

In our case we use `Fifo`, so the system best follows the swapchain behavior described previously.

```Cpp
config.presentMode = WGPUPresentMode_Fifo;
```

Finally, we may specify how the textures will be composited onto the OS window, which may be used to create transparent windows. Alternatively, it could be left to auto mode:

> Troubleshooting
> If you get the error `Uncaptured device error: type 3 (Device(OutOfMemory))` when calling `wgpuSurfaceConfigure`, check that you specified the `GLFW_NO_API` value to glfw when creating the window.

## Surface Texture

Once the surface is configured it is possible to request it at each frame for the next available texture in the swap chain, the texture onto which we must draw. The content of our main loop is like:

```Cpp
// In Application::MainLoop()
{{Get the next target texture view}}
{{Draw things}}
{{Present the surface onto the window}}
```

Create a dedicated function `GetNextSurfaceTextureView()` in the application class. A texture view is usually what is needed instead of a raw surface texture:

```Cpp
WGPUTextureView Application::GetNextSurfaceTextureView() {
    {{Get the next surface texture}}
    {{Create surface texture view}}
    return targetView;
}
```

In the main loop we need to call this function at the very beginning and check that it returns a valid view:

```Cpp
// Get the next target texture view
WGPUTextureView targetView = GetNextSurfaceTextureView();
if (!targetView) return;
```

>Note
> Do not forget to declare the function in the application class declaration. This method is critical, but should not be polled by users directly. It is a private method.

### Getting the next target texture

To get the texture to draw onto, we use `wgpuSurfaceGetCurrentTexture`. The "surface texture" is not really an object rather it is a container for the multiple things this function returns. It is up to us to create the `WGPUSurfaceTexture` container, pass this into the function to write to it:

```Cpp
WGPUSurfaceTexture surfaceTexture;
wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
```

This provides the following information:
* `surfaceTexture.status` tells us whether the operation was successful, and if not gives some hint about why.
* `surfaceTexture.suboptimal` may note, despite the texture being successfully retrieved, the underlying surface changed and we should probably reconfigure it.
* `surfaceTexture.texture` is the texture that we must draw on during this frame.

Only deal with the obvious failure case and ignore the suboptimal flag for now:

```Cpp
if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
    return nullptr;
}
```

### Texture view

This is not a surface texture, but a texture view. It may represent a sub-part of the texture, or expose it using a different format. Texture views are covered in more detail in the Texturing sectino of this guide, simply copy and post the following boilerplate for now:

```Cpp
WGPUTextureViewDescriptor viewDescriptor;
viewDescriptor.nextInChain = nullptr;
viewDescriptor.label = "Surface texture view";
viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
viewDescriptor.dimension = WGPUTextureViewDimension_2D;
viewDescriptor.baseMipLevel = 0;
viewDescriptor.mipLevelCount = 1;
viewDescriptor.baseArrayLayer = 0;
viewDescriptor.arrayLayerCount = 1;
viewDescriptor.aspect = WGPUTextureAspect_All;
WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);
```

The view must be released once it is no longer needed, just before presenting:

```Cpp
// At the end of the frame
wgpuTextureViewRelease(targetView);
```

### Presenting

Once the texture is filled in and released, we can tell the surface to present the next texture of its swap chain(which may or may not be the texture just drawn, depending on `presentMode`):

```Cpp
wgpuSurfacePresent(surface);
```

> Building for the Web
> In the context of a Web browser, we do not present the surface texture ourselves. Instead `emscripten_set_main_loop_arg` (aka `requestAnimationFrame` in JS) to call our `MainLoop()` function right before presenting.
>
>As a consequence, do NOT call `wgpuSurfacePresent()` when building with emscripten:
>```Cpp
>// At the end of the frame
>wgpuTextureViewRelease(targetView);
>#ifndef __EMSCRIPTEN__
>wgpuSurfacePresent(surface);
>#endif
>```

# Render Pass

## Render pass encoder

GPU-side operations are triggered from the command queue, using a command encoder as described in the command queue.

First build the WGPUCommandEncoder. Second encode the render pass, in our case clear the screen with a uniform color. Lastly finish the encode and submit it.

```Cpp
{{Create Command Encoder}}
{{Encode Render Pass}}
{{Finish encoding and submit}}
```

Carefully review webgpu.h at the methods of the encoder(the procedures starting with `wgpuCommandEncoder`), most of them are related to copying buffers and textures around. Except for two: `wgpuCommandEncoderBeginComputePass` and `wgpuCommandEndoderBeginRenderPass`. These return specialized encoder objects, namely `WGPUComputePassEncoder` and `WPURenderPassEncoder`. These give access to commands dedicated to computing and rendering respectively.

In our case we use a render pass:

```Cpp
WGPURenderPassDescriptor renderPassDesc = {};
renderPassDesc.nextInChain = nullptr;

{{Describe Render Pass}}

WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
{{Use Render Pass}}
wgpuRenderPassEncoderEnd(renderPass);
wgpuRenderPassEncoderRelease(renderPass);
```

Directly end the pass without issuing any other command. This is because the render pass has a built-in mechanism for clearing the screen when it begins, set it up through the descriptor:

```Cpp
// Use the render pass here (we do nothing with the render pass for now)
```

### Color attachment

A render pass leverages the rendering circuitry of the GPU to draw content into one or multiple textures. So one important thing to set up is to tell which textures are the target of this process. These are the attachments of the render pass.

The number of attachment is variable, so the descriptor gets it through two fields: the number `colorAttachementCount` of attachments and the address `colorAttachments` of the color attachment array. Since we only use one here, the address of the array is just the address of a single `WGPURenderPassColorAttachment` variable.

```Cpp
WGPURenderPassColorAttachment renderPassColorAttachment = {};

{{Describe the attachment}}

renderPassDesc.colorAttachmentCount = 1;
renderPassDesc.colorAttachments = &renderPassColorAttachment;
```

The first important setting of the attachment is the texture view it must draw in.

In our case, this is simply the `targetView` that we got from the surface, because we want to directly draw on screen, but in advanced pipelines it is very common to draw on intermediate textures, which are then fed to post-processing passes.

```Cpp
renderPassColorAttachment.view = targetView;
```

There is a second target texture view called `resolveTarget`, but it is not relevant here because we do not use multi-sampling(covered later).

```Cpp
renderPassColorAttachment.resolveTarget = nullptr;
```

The `loadOp` setting indicates the load operation to perform on the view prior to executing the render pass. It can be either read from the view or set to a default uniform color, namely the clear value. When it does not matter use `WGPULoadOp_Clear` it is likely more efficient.

The `storeOp` indicates the operation to perform on view after executing the render pass. It can be either stored or discarded(the latter only makes sense if the render pass has side-effects).

`clearValue` is the value to clear the screen with. Put anything you want in here! The four values are red, green, blue, and alpha channels, on a scale from 0.0 to 1.0.

```Cpp
renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
```

There is a last member `depthSlice` to set in the attachment, it must be explicitly set to its undefined value because we do not use a depth buffer. This option is not supported by `wgpu-native` for now so we enclosed this within a `#ifdef`:

```Cpp
#ifndef WEBGPU_BACKEND_WGPU
renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU
```

### Misc

There is another special type of attachment, the depth and stencil attachment(a single attachment with potentially two channels). It is not used for now, set it to null:

```Cpp
renderPassDesc.depthStencilAttachment = nullptr;
```

When measuring the performance of a render pass, it is not possible to use CPU-side timing functions, since the commands are not executed synchronously. Instead, the render pass can receive a set of timestep queries. We do not use it in this example(this is covered in an advanced chapter about Benchmarking Time, check it out later):

```Cpp
renderPassDesc.timestampWrites = nullptr;
```

# Conclusion

Once you build and run your app you should be able to get a colored window. Although simple, there were a lot of important concepts covered.

* Instead of directly drawing to the windows' surface, draw to an off screen texture and the swap chain is responsible for managing the texture turn over.

* The 3D rendering pipeline of the GPU is leveraged through the render pass, which is a special scope of commands accessible through the command encoder.

* The render pass draws to one or multiple attachments, which are texture views.

> Note
> When using Dawn, the displayed color is potentially different because the surface color format uses another color space. This is addressed later!

This is the basic WebGPU setup. From here on out it is possible to start 3D rendering! The next chapter is a bonus that introduces a more comfortable API that benefits from C++ idioms.

Step025
