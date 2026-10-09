
Step_052_cpp

# Depth buffer

>[!IMPORTANT]
>Marked as written for an OLDER version of WebGPU, but every depth-related field (`RenderPassDepthStencilAttachment`, `DepthStencilState`) is IDENTICAL in our three headers (wgpu-native v0.19, Dawn 6536, emscripten 3.1.63). Only the surrounding code (SwapChain, everything in `main`) had to be translated.

## The Z-Buffer algorithm

In the last chapter the faces were drawn in the wrong order: the GPU paints triangles in the order of the index buffer and the LAST one wins, even if it is behind.

The **Z-buffer** (or depth buffer) fixes this. It is a texture the size of the window that stores, for each pixel, the depth of the closest fragment drawn so far. When a new fragment arrives:

1. The GPU compares its depth with the one stored for that pixel.
2. If it is **closer** (lower), it is drawn and its depth is stored.
3. If it is **farther**, it is thrown away.

At the start of each frame the buffer is cleared to 1.0, which means "as far as possible".

>[!note]
>The only cost is the memory of the depth texture: 640 x 480 x 4 bytes, about 1.2 MB.

## Pipeline State

The pipeline gets a `DepthStencilState`:

```Cpp
wgpu::DepthStencilState depthStencilState = wgpu::Default;
depthStencilState.depthCompare = wgpu::CompareFunction::Less;// keep the closest
depthStencilState.depthWriteEnabled = true;// remember the depth of what we keep
depthStencilState.format = depthTextureFormat;// Depth24Plus
depthStencilState.stencilReadMask = 0;// no stencil
depthStencilState.stencilWriteMask = 0;
pipelineDesc.depthStencil = &depthStencilState;
```

>[!note]
>The default `depthCompare` is `Always`: every fragment passes, which is the same as having no depth test.

>[!IMPORTANT]
>Transparency breaks the rule "the lowest depth is visible": a see-through triangle in front still writes its depth and hides what is behind it. With alpha blending, draw order matters again.

## Depth texture

Depth textures have their own formats. `Depth24Plus` = at least 24 bits of depth. Depth and stencil usually share 32 bits per pixel: 24 for the depth, 8 for the stencil.

```Cpp
depthTextureDesc.size = {640, 480, 1};// MUST match the surface size
depthTextureDesc.usage = wgpu::TextureUsage::RenderAttachment;
depthTexture = device.createTexture(depthTextureDesc);
```

The render pass does not take the texture itself, it takes a **view** of it (which mip level, which layer, which aspect). We want the whole texture, depth only: `aspect = wgpu::TextureAspect::DepthOnly`.

New device limits: `maxTextureDimension2D = 640`, `maxTextureDimension1D = 480`, `maxTextureArrayLayers = 1`.

>[!note]
>Release order in `Terminate()`: the view, then `destroy()` (frees the GPU memory now) and `release()` (drops our handle) on the texture.

## Depth attachment

The render pass gets a depth attachment next to the color one: clear to 1.0, store the result.

>[!IMPORTANT]
>`Depth24Plus` has NO stencil part, so the stencil ops must be `LoadOp::Undefined` / `StoreOp::Undefined` (Dawn and Chrome refuse anything else).
>
>The guide says wgpu-native needs `Clear` / `Store` instead and adds an `#ifdef WEBGPU_BACKEND_WGPU`. We TESTED `Undefined` with our wgpu-native (v0.19): no error, same picture. So we use `Undefined` on all three builds and skip the `#ifdef`. The guide's advice was probably true for an older wgpu-native.
>```Cpp
>depthStencilAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
>depthStencilAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;
>```

## Shader

The vertex shader must now output the REAL depth instead of 0.0. WebGPU only keeps depths between 0.0 (near) and 1.0 (far), and our z goes from about -1 to 1, so we squeeze it:

```rust
out.position = vec4f(position.x, position.y * ratio, position.z * 0.5 + 0.5, 1.0);
```

>[!note]
>`z * 0.5 + 0.5` is a temporary trick. The projection matrix (two chapters from now) does this properly.

## Conclusion

The faces are now in the right order on all three builds. The pyramid still looks FLAT because there is no perspective: far things are not smaller. That is "Transformation matrices" and "Projection matrices".

Resulting code: branch `step052` of LearnWebGPU-Code.
