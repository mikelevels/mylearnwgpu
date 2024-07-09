
Step032

# A first Vertex Attribute

In the Hello Triangle chapter, we were hardcoding the three vertex positions directly in the shader, obviously hardcoding does not scale. In this chapter, we see the proper way to feed vertex attributes as input to the vertex shader.

## Vertex Shader Input

Remember that we do not control the way the `vs_main` function is invoked, the fixed part of the render pipeline does. However, we can request some input data by labeling the argument of the function with WGSL attributes.

>Important
>The term attribute is used for two different things. We talk about WGSL attribute to mean tokens of the form `@something` in a WGSL code, and about vertex attribute to mean an input of the vertex shader.

We already use a WGSL attribute in our vertex shader input actually, namely the `@builtin(vertex_index)` attribute:

```Cpp
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> /* [...] */ {
    //     ^^^^^^^^^^^^^^^^^^^^^^ This is a WGSL attribute
    // [...]
}
```

This means the argument `in_vertex_index` must be populated by the vertex fetch stage with the index of the current vertex.

>Note
>Attributes that are built-in, like `vertex_index`, are listed in the WGSL documentation.

Instead of using a built-in input, we can create our own. For this we need to:

1. Create a buffer to store the value of the input for each vertex; this data must be stored on the GPU side of course.

2. Tell the render pipeline how to interpret the raw buffer data when fetching an entry for each vertex. This is the vertex buffer layout.

3. Set the vertex buffer in the render pass before the draw call.

On the shader side, we replace the vertex index argument with a new one:

```Cpp
@vertex
fn vs_main(@location(0) in_vertex_position: vec2f) -> /* [...] */ {
    //     ^^^^^^^^^^^^ This is a WGSL attribute
    // [...]
}
```

The `@location(0)` attribute means that this input variable is described by the first (index '0') vertex attribute in the `pipelineDesc.vertex.buffers` array. The type `vec2f` must comply with what we will declare in the layout. The argument name `in_vertex_position` is up to you, it is only local to the shader code!

The vertex shader becomes really simple in the end:

```Cpp
fn vs_main(@location(0) in_vertex_position: vec2f) -> @builtin(position) vec4f {
    return vec4f(in_vertex_position, 0.0, 1.0);
}
```

```Cpp
const char* shaderSource = R"(
@vertex
{{Vertex shader}}

@fragment
{{Fragment shader}}
)";
```

```Cpp
fn fs_main() -> @location(0) vec4f {
    return vec4f(0.0, 0.4, 1.0, 1.0);
}
```

## Device Capabilities

### Checking Capabilities

What is the maximum number of location attributes?

The number of vertex attributes available for our device may vary if we do not specify anything. We can check it as follows:


```Cpp
SupportedLimits supportedLimits;

adapter.getLimits(&supportedLimits);
std::cout << "adapter.maxVertexAttributes: " << supportedLimits.limits.maxVertexAttributes << std::endl;

device.getLimits(&supportedLimits);
std::cout << "device.maxVertexAttributes: " << supportedLimits.limits.maxVertexAttributes << std::endl;

// Personally I get:
//   adapter.maxVertexAttributes: 32
//   device.maxVertexAttributes: 16
```

The spirit of the adapter+device abstraction provided by WebGPU is to first check on the adapter that it has capabilities we need, then we require the minimal limits needed during the device creation and if the creation succeeds we are guaranteed to have the limits we asked for.

And we get nothing more than required, so that if we forget to update the initial check when using more vertex buffers, the program fails. With this good practice, we limit the cases of "it worked for me" where the program runs correctly on your device but not on somebody else's, which can quickly become a nightmare.

### Requiring capabilities

This initial check is done by specifying a non null `requiredLimits` pointer in the device descriptor. I suggest that we create a method `GetRequiredLimits(Adapter adapter)` dedicated to setting our app requirements:

```Cpp
// Before adapter.requestDevice(deviceDesc)
RequiredLimits requiredLimits = GetRequiredLimits(adapter);
deviceDesc.requiredLimits = &requiredLimits;
```

```Cpp
// In Application class
private:
    RequiredLimits GetRequiredLimits(Adapter adapter) const;
```

> Note
>We do note really need the adapter to determine the required limits in our case, but I pass it as an argument because it can become useful in a scenario where a different quality of device is requested depending on the adapter's capabilities.

The required limits follow the same structure than the supported limits, we can customize some of them for our vertex attribute:

```Cpp
RequiredLimits Application::GetRequiredLimits(Adapter adapter) const {
    // Get adapter supported limits, in case we need them
    SupportedLimits supportedLimits;
    adapter.getLimits(&supportedLimits);

    // Don't forget to = Default
    RequiredLimits requiredLimits = Default;

    // We use at most 1 vertex attribute for now
    requiredLimits.limits.maxVertexAttributes = 1;
    // We should also tell that we use 1 vertex buffers
    requiredLimits.limits.maxVertexBuffers = 1;
    // Maximum size of a buffer is 6 vertices of 2 float each
    requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
    // Maximum stride between 2 consecutive vertices in the vertex buffer
    requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);
    // This must be set even if we do not use storage buffers for now
    requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

    return requiredLimits;
}
```

> Important
> Notice how I initialized the required limits object with `= Default`d above. This is a syntactic helper provided by the webgpu.hpp wrapper for all structs to prevent us from manually setting default values. In this case it sets all limits to `WGPU_LIMIT_U32_UNDEFINED` or `WGPU_LIMIT_U64_UNDEFINED` to mean that there is no requirement.

```Cpp
{{GetRequiredLimits method}}
```

I now get these more secure supported limits:

```Cpp
adapter.maxVertexAttributes: 32
device.maxVertexBuffers: 1
```

I recommend you have a look at all the fields of the WGPULimits structure in `webgpu.h` so that you know when to add something to the required limits.

>Note
> There are two limits that may cause issue even if set to `WGPU_LIMIT_U32_UNDEFINED`, namely the one that set minimums rather than maximums. The default "undefined" value may not be supported by the adapter, so in this case only I suggest we forward values from the supported limits:
>```Cpp
>// These two limits are different because they are "minimum" limits,
>// they are the only ones we are may forward from the adapter's supported
>// limits.
>requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
>requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
>```

Additionally, there was a new error reported right after resolving this one. You needed to specify the max texture dimensions as well! All of this implied that the null values were set for the device object at some point before setting the new required limits. So you added some new code to resolve the issue in Application::GetRequiredLimits().

### Default Capabilities

The official default capabilities can be found in [the specification](https://www.w3.org/TR/webgpu/#limit-default). This also mentions every adapter is guaranteed to support the default value or better.

In this guide, Eli Michel took care of specifying required capabilities more strictly for two reasons:

* To make sure we present the capabilities related to the various notions as we are introducing them.
* Because it is possible to have "compatability" adapters that would not be deemed valid in the web but are still exposed in native mode.

The list is very large. Visit the specification page for details.

## Vertex Buffer

Back to our vertex position attribute: for now we hard-code the values of the vertex buffer in the C++ source:

```Cpp
// Vertex buffer data
// There are 2 floats per vertex, one for x and one for y.
// But in the end this is just a bunch of floats to the eyes of the GPU,
// the *layout* will tell how to interpret this.
std::vector<float> vertexData = {
    // x0, y0
    -0.5, -0.5,

    // x1, y1
    +0.5, -0.5,

    // x2, y2
    +0.0, +0.5
};

// We will declare vertexCount as a member of the Application class
vertexCount = static_cast<uint32_t>(vertexData.size() / 2);
```

The GPU-side vertex buffer is created like any other buffer, as introduced in the previous chapter. The main difference is that we must specify `BufferUsage::Vertex` in its `usage` field.

```Cpp
// Create vertex buffer
BufferDescriptor bufferDesc;
bufferDesc.size = vertexData.size() * sizeof(float);
bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
bufferDesc.mappedAtCreation = false;
vertexBuffer = device.createBuffer(bufferDesc);

// Upload geometry data to the buffer
queue.writeBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);
```

We declare `vertexBuffer` and `vertexCount` as members of our `Application` class and place this initialization in a dedicated `InitializeBuffers()` method:

```Cpp
private: // Application attributes
    Buffer vertexBuffer;
    uint32_t vertexCount;
```

```Cpp
private: // APplication methods
    void InitializeBuffers();
```

This method define the CPU-side vertex data and then creates the GPU-side buffer as described above:

```Cpp
void Application::InitializeBuffers() {
    {{Define vertex data}}
    {{Create vertex buffer}}
}
```

```Cpp
{{InitializeBuffers method}}
```

>Note
>Do not forget to call this at the end of `Initialize()`:
>```Cpp
>// At the end of Initialize()
>InitializeBuffers();
>```

And of course we release this buffer when the application terminates:

```Cpp
// At the beginning of Terminate()
vertexBuffer.release();
```

# Vertex Buffer Layout

For the vertex fetch stage to transform this raw data from the vertex buffer into what the vertex shader expects, we need to specify a `VertexBufferLayout` to `pipelineDesc.vertex.buffers`:

```Cpp
// Vertex fetch
VertexBufferLayout vertexBufferLayout;
{{Describe the vertex buffer layout}}

pipelineDesc.vertex.bufferCount = 1;
pipelineDesc.vertex.buffers = &vertexBufferLayout;
```

Note that a given render pipeline may use more than one vertex buffer. On the other hand, the same vertex buffer can contain multiple vertex attributes.

>Note
>This is why the `maxVertexAttributes` and `maxVertexBuffers` limits are different concepts.

So, within our vertex buffer layout, we specify how many attributes it contains and detail each of them. In our case, there is only the position attribute:

```Cpp
VertexAttribute positionAttrib;
{{Describe the position attribute}}

vertexBufferLayout.attributeCount = 1;
vertexBufferLayout.attributes = &positionAttrib;

{{Describe buffer stride and step mode}}
```

### For each attribute

We can now configure our vertex attribute:

* The value of `shaderLocation` must be the same as what specifies the WGSL attribute `@location(...)` in the vertex shader.

* The format `Float32x2` corresponds at the same time to the type `vec2f` in the shader and to the sequence of 2 floats in the vertex buffer data.

* The offset tells where the sequence of position values start in the raw vertex buffer. In our case it starts at the beginning (offset 0). It is non null when the same vertex buffer contains multiple attributes.

```Cpp
// == For each attribute, describe its layout, i.e., how to interpret the raw data ==
// Corresponds to @location(...)
positionAttrib.shaderLocation = 0;
// Means vec2f in the shader
positionAttrib.format = VertexFormat::Float32x2;
// Index of the first element
positionAttrib.offset = 0;
```

### For the whole vertex buffer

Attributes coming from the same vertex buffer share some properties:

* The stride is a common concept in buffer manipulation: it designates the number of bytes between two consecutive elements. In our case, the positions are contiguous so the stride is equal to the size of a `vec2f`, but this will change when adding more attributes in the same buffer, if attributes are interleaved.

* Finally the `stepMode` is set to `Vertex` to mean that each value from the buffer corresponds to a different vertex. The step mode is set to `Instance` when each value is shared by all vertices of the same instance(i.e., copy) of the shape(we'll see instancing later on).

```Cpp
// == Common to attributes from the same buffer ==
vertexBufferLayout.arrayStride = 2 * sizeof(float);
vertexBufferLayout.stepMode = VertexStepMode::Vertex;
```

# Render Pass

The last change we need to apply is to "connect" the vertex buffer to the pipeline's vertex buffer layout when encoding the render pass:

```Cpp
renderPass.setPipeline(pipeline);

// Set vertex buffer while encoding the render pass
renderPass.setVertexBuffer(0, vertexBuffer, 0, vertexBuffer.getSize());

// We use the `vertexCount` variable instead of hard-coding the vertex count
renderPass.draw(vertexCount, 1, 0, 0);
```

And we get... exactly the same triangle as before. Except we can very easily add some geometry:

```Cpp
// Vertex buffer data
// There are 2 floats per vertex, one for x and one for y.
std::vector<float> vertexData = {
    // Define a first triangle:
    -0.5, -0.5,
    +0.5, -0.5,
    +0.0, +0.5,

    // Add a second triangle:
    -0.55f, -0.5,
    -0.05f, +0.5,
    -0.55f, +0.5
};
```

# Conclusion

We have seen in this chapter how to use GPU buffers to feed data as input to the vertex shader, and thus to the whole rasterization pipeline. We will refine this in the next chapter by adding additional attributes.

Step032

IMPORTANT NOTE:
There was a fix issued for an bug in the code at this point. The solution was posted to the web book. Notably, this lead to yet another error. There was an issue requesting a texture beyond a size of 0. This indicates the limits had been set at some point in the code to unreasonable null or low values.

YOU SUSPECT THIS OCCURS DURING THE CALL TO Application::Initialize();. All the other issues appeared to be cropping up in the call to Initialize();.


