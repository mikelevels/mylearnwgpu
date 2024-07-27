
Step034

# Index Buffer

The index buffer is used to seperate the list of vertex attributes from the actual order in which they are connected. Now lets try drawing a square:

## Index Data

A straightforward way of drawing a square is to use the following vertex attributes:

```Cpp
std::vector<float> vertexData = {
    // Triangle #0
    -0.5, -0.5, // A
    +0.5, -0.5,
    +0.5, +0.5, // C

    // Triangle #1
    -0.5, -0.5, // A
    +0.5, +0.5, // C
    -0.5, +0.5,
};
```

But as you can see some data is duplicated(points $A$ and $C$). And this duplication could be much worst on larger shapes with many more connected triangles.

A more compact way of expressing the square's geometry is to seperate the position from the connectivitiy:

```Cpp
// Define point data
// The de-duplicated list of point positions
std::vector<float> pointData = {
    -0.5, -0.5, // Point #0 (A)
    +0.5, -0.5, // Point #1
    +0.5, +0.5, // Point #2 (C)
    -0.5, +0.5, // Point #3
};

// Define index data
// This is a list of indices referencing positions in the pointData
std::vector<uint16_t> indexData = {
    0, 1, 2, // Triangle #0 connects points #0, #1 and #2
    0, 2, 3  // Triangle #1 connects points #0, #2 and #3
};
```

The index data must have type `uint16_t` or `uint32_t`. The former is more compact but limited to $2^16 = 65536$ vertices.

> [!note]
> I also keep the interleaved color attribute in this example, my point data is:
>```Cpp
>std::vector<float> pointData = {
>   // x,   y,     r,   g,   b
>   -0.5, -0.5,   1.0, 0.0, 0.0,
>   +0.5, -0.5,   0.0, 1.0, 0.0,
>   +0.5, +0.5,   0.0, 0.0, 1.0,
>   -0.5, +0.5,   1.0, 1.0, 0.0
>};
>```
>
>```Cpp
>// This is a list of indices referencing positions in the pointData
>std::vector<uint16_t> indexData = {
>   0, 1, 2, // Triangle #0 connects points #0, #1 and #2
>   0, 2, 3  // Triangle #1 connects points #0, #2 and #3
>};
>```
>Using the index buffer adds an *overhead* of `6*sizeof(uint16_t)` = 12 bytes *but also saves* `2*5*sizeof(float)` = 40 bytes. Therefore even on this very simple example it is worth using!

This split of data reorganizes a bit of the buffer initialization method:

```Cpp
void Application::InitializeBuffers() {
    // [...] Define point data
    // [...] Define index data
    
    // We now store the index count rather than the vertex count
    indexCount = static_cast<uint32_t>(indexData.size());

    // [...] Create point buffer
    // [...] Create index buffer
}
```

>[!note]
>I usually replace the name vertex data with point data when referring to the de-duplicated attribute buffer. In other terms, `vertex[i] = points[index[i]]`. The name vertex is used to mean a corner of triangle, i.e., a pair of a point and a triangle that uses it.
>
>```Cpp
>// Create point buffer
>BufferDescriptor bufferDesc;
>bufferDesc.size = pointData.size() * sizeof(float);
>bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
>bufferDesc.mappedAtCreation = false;
>pointBuffer = device.createBuffer(bufferDesc);
>
>// Upload geometry data to the buffer
>queue.writeBuffer(pointBuffer, 0, pointData.data(), bufferDesc.size);
>```
>
>```Cpp
>// Create point buffer
>WGPUBufferDescriptor bufferDesc{};
>bufferDesc.nextInChain = nullptr;
>bufferDesc.size = pointData.size() * sizeof(float);
>bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex; // Vertex usage here!
>bufferDesc.mappedAtCreation = false;
>pointBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
>
>// Upload geometry data to the buffer
>wgpuQueueWriteBuffer(queue, pointBuffer, 0, pointData.data(), bufferDesc.size);
>```

In the list of *application attributes*, replace `vertexBuffer` with `pointBuffer` and `indexBuffer`, and replace `vertexCount` with `indexCount`.

```Cpp
private: // Application attributes
    Buffer pointBuffer;
    Buffer indexBuffer;
    uint32_t indexCount;
```

And as usual, we release buffer in `Terminate()`

```Cpp
pointBuffer.release();
indexBuffer.release();
```

```Cpp
// It is not easy with the auto-generation of code to remove the previously
// defined `vertexBuffer` attribute, but at the same time some compilers
// (rightfully) complain if we do not use it. This is a hack to mark the
// variable as used and have automated build tests pass.
(void)vertexBuffer;
(void)vertexCount;
```

## Buffer Creation

Of course the *index data* must be stored in a *GPU-side buffer*. This buffer needs a usage of `BufferUsage::Index`.

```Cpp
// Create index buffer
// (we reuse the bufferDesc initialized for the vertexBuffer)
bufferDesc.size = indexData.size() * sizeof(uint16_t);
{{Fix buffer size}}
bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
indexBuffer = device.createBuffer(bufferDesc);

queue.writeBuffer(indexBuffer, 0, indexData.data(), bufferDesc.size);
```

>[!important]
> A `writeBuffer` operation must copy a number of bytes that is a *multiple of 4*. To ensure this, we must *ceil the buffer size* up to the next multiple of 4 creating it:
>```Cpp
>bufferDesc.size = (bufferDesc.size+3) & ~3;//round up to the next multiple of 4
>```

## Render pass

To draw with an index buffer, there are *two changes* in the render pass encoding:

1) Set the active index buffer with `renderPass.setIndexBuffer`.
2) Replace `draw()` with `drawIndexed()`.

```Cpp
renderPass.setPipeline(pipeline);

// Set both vertex and index buffers
renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());
// The second argument must correspond to the choice of uint16_t or uint32_t
// we've done when creating the index buffer.
renderPass.setIndexBuffer(indexBuffer, IndexFormat::Uint16, 0, indexBuffer.getSize());

// Replace `draw()` with `drawIndexed()` and `vertexCount` with `indexCount`
// The extra argument is an offset within the index buffer.
renderPass.drawIndexed(indexCount, 1, 0, 0, 0);
```

We now see the "square", with color highlighting with the red and blue points($A$ and $C$) are shared by both triangles. This results in a square with a saturated gradient line down the middle.

(The square is deformed because of the windows's aspect ratio.)

### Ratio Correction

The square is deformed because its coordinates are expressed relative to the window's dimensions. This is easily resolved by multiplying one of the coordinates by the ratio of the window($$\frac{640}{480}$$ in our case).

The ratio could be applied in the initial vertex data vector, but this will require us to update these values whever the window dimension changes. A more interesting option is to use the power of the vertex shader:

```rust
// In vs_main():
let ratio = 640.0 / 480.0; // The width and height of the target surface
out.position = vec4f(in.position.x, in.position.y * ratio, 0.0, 1.0);
```

```rust
var out: VertexOutput; // create the output struct
{{Vertex shader position}}
out.color = in.color; // forward the color attribute to the fragment shader
return out;
```

Although basic, this is a first step towards what will be the key use of the vertex shader when introducing 3D transforms.

>[!note]
> It might feel a little unsatisfying to hard-code the window resolution in the shader like this, this is addressed later with uniforms.

# Conclusion

Using an index buffer is a rather simple concept in the end, and it can save a lot of VRAM(GPU memory).

Additionally, it corresponds to the way traditional formats usually encode 3D meshes in order to keep the connectivity information, which is important for authoring(so that when the user moves a points all triangles that share this point are affected).

```Cpp
// Change background color
renderPassColorAttachment.view = targetView;
renderPassColorAttachment.resolveTarget = nullptr;
renderPassColorAttachment.loadOp = LoadOp::Clear;
renderPassColorAttachment.storeOp = StoreOp::Store;
renderPassColorAttachment.clearValue = Color{ 0.05, 0.05, 0.05, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU
```

Step034