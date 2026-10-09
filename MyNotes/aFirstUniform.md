
Step_039_cpp

# A First Uniform

## Introduction

### Motivation

If we look at our current shader, we see some __hard-coded__ constants:

```rust
let ratio = 640.0/480.0;
let offset = vec2f(-0.6875, -0.463);
```

What if we want to move the logo drawn on screen while the application is running? Or the user resizes the render window?

We could dynamically change the shader code and rebuild a new shader module!!!

Honestly, changing the shader module every time there is a change would work. **Building a shader module takes time**. From experience it takes a lot of time when compared to the desired time budget of 60 fps for reasonably good performance. Imagine if there is an animation playing in the scene and we want to change the `offset` in each frame?

This is why the preferred solution is to use a **uniform variable**.

### Definition

A uniform is a global variable in a shader whose value is loaded from a GPU buffer. We say that it is **bound** to a buffer.

Its value is **uniform** across the different vertices and fragment of a **given call** to `draw`, but it can be changed from one call to another by **updating** the value of the buffer it is *bound* to.

To use a uniform, we need to: 

    1. Declare the uniform in the **shader**.
    2. Create the **buffer** it is bound to.
    3. Configure the properties of the **binding** (a.k.a the binding's **layout**).
    4. Create the **bind group**.

### Device limits

In this chapter, we will require the following limits to be set up for our device:

## Shader side

In order to animate our scene, we create a uniform called `uTime` that we update each frame with the current time, expressed in seconds (as provided by `glfwGetTime()`).

>[!note]
>It is recommended to **prefix** uniform variables with 'unif' so that it is easy to identify **uniforms from local variables**.

As stated, a uniform is a global variable, we declare it in the first line of our shader file. A variable declaration in WGSL looks as follows:

```rust
var uTime: f32;
```

`var` is a keyword and it can be labeled with an **address space**. **Address space** controls how the variable is store in the GPU. In the code below we add _where_ the variable is stored:

```rust
// Variable in the *uniform* address space
var<uniform> uTime: f32;
```

Now we need to specify *which buffer* the uniform is *bound to*. Add a **binding index** with the `@binding(0)` WGSL attribute:

```rust
// Specify which binding index the uniform is attached to
@binding(0) var<uniform> uTime: f32;
```

Don't forget bindings are organized in **bind groups**, so the binding uniform is specified by providing a `@group(...)` attribute:

```rust
// The memory location of the uniform is given by a pair of a *bind group* and a *binding*
@group(0) @binding(0) var<uniform> uTime: f32;
```

Now the declaration of the uniform variable is complete, it can be used like any other variable in our shader:

```rust
//We add the declaration of 'uTime' to the shader prelude
{{Declare uniforms}}
```

```rust
fn vs_main(in: VertexInput)->VertexOutput{
    var out: VertexOutput;
    let ratio = 640.0/480.0;

    //Now move the scene depending on the time!
    var offset = vec2f(-0.6875, -0.463);
    offset += 0.3 * vec2f(cos(uTime), sin(uTime));

    out.position = vec4f(in.position.x + offset.x, (in.position.y + offset.y)*ratio, 0.0, 1.0);
    out.color = in.color;
    return out;
}
```

## Uniform Buffer

The uniform buffer is created like any other buffer, except we must specify `BufferUsage::Uniform` in its `usage` field. We only need it to contain 1 float for now, but **buffer size needs to be aligned to 16 bytes**, so we create a buffer of 4 floats (1 float uses 4 bytes).

We first declare our `uniformBuffer` in the `Application` class attributes:

```Cpp
private: // Application attributes
    Buffer uniformBuffer;
```

Create the buffer during initialization:

```Cpp
//Create uniform buffer (reusing bufferDesc from other buffer creations)
// The buffer will only contain 1 float with the value of uTime
// then 3 floats left empty but needed by alignment constraints
bufferDesc.size = 4*sizeof(float);

//Make sure to flag the buffer as BufferUsage::Uniform
bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;

bufferDesc.mappedAtCreation = false;
uniformBuffer = device.createBuffer(bufferDesc);
```

Then use `Queue::writeBuffer` to upload a value in the first float of the buffer:

```Cpp
float currentTime = 1.0f;
queue.writeBuffer(uniformBuffer, 0, &currentTime, sizeof(float));
```

To summarize, we put this at the end of `Application::InitializeBuffers()`:

```Cpp
void Application::InitializeBuffers(){
    // 1. Load from disk into CPU-side vectors pointData and indexData
    {{Load geometry data from file}}

    // 2. Create GPU buffers and upload data to them
    {{Create point buffer}} 
    {{Create index buffer}}

    // 3. Create and fill uniform buffer <-- HERE
    {{Create uniform buffer}}
    {{Upload uniform values}}
}
```

>[!note]
> Do not forget to release these buffer in the `Terminate()` method:
> ```Cpp
> uniformBuffer.release();
> ```

## Binding Configuration

If you try to run your program now, you will hit a device error stating that some bind group referenced in the shader is **incompatible** with the expected bind group layout. What does this mean?

There are two steps in the actual connection of the buffer to the uniform. Firstly we need to specify in the pipeline how and what we want in the binding. This is the binding layout. Secondly, we need to create the bind group and enable it(see next section).

>[!note]
> This follows the same spirit than the distinction between the *vertex buffer* and the *vertex buffer layout*.

### Pipeline layout

Spcify the binding layout in the `PipelineLayout` part of the pipeline descriptor. The pipeline layout describes how all the **resources** used by the render pipeline must be bound.

A resource is either a texture or a buffer, and its layout specifies to which index it is bound to and properties like whether it is accessed as read-only or write-only, etc.

To date our pipeline has been implicitly laid out by setting `pipelineDesc.layout`, but from now on we will explicitly declare what resources our pipeline expects:

```Cpp
{{Create pipeline layout}}

// Assign the PipelineLayout to the RenderPipelineDescriptor's layout field
pipelineDesc.layout = layout;
```

When creating this pipeline layout, we may define multiple **bind groups**, and a bind group contains multiple **bindings**:

```Cpp
{{Define bindingLayout}}

//Create a bind group layout
BindGroupLayoutDescriptor bindGroupLayoutDesc{};
bindGroupLayoutDesc.entryCount = 1;
bindGroupLayoutDesc.entries = &bindingLayout;
bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

// Create the pipeline layout
PipelineLayoutDescriptor layoutDesc{};
layoutDesc.bindGroupLayoutCount = 1;
layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&bindGroupLayout;
layout = device.createPipelineLayout(layoutDesc); 
```


