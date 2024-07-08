
Step031

# Playing with buffers

# WITH webgpu.hpp

Before feeding vertex data to the render pipeline, we need to get familiar with the notion of a buffer. A buffer is "just" a chunk of memory allocated in the VRAM(the GPU's memory). Think of it as some kind of `new` or `malloc` for the GPU.

In this chapter, we see how to create (ie. allocate), write from CPU, copy from GPU to GPU and read back to CPU.

>Note
> Textures are a special kind of memory(because of the way we usually sample them) that they live in a different kind of object.

Since this is just an expirement, temporarily write all of the code of this chapter at the end of the `Initialize()` function. The overall outlien of our code will be:

```Cpp
// Experimentation for the "Playing with buffer" chapter
{{Create a first buffer}}
{{Create a second buffer}}

{{Write input data}}

{{Encode and submit the buffer to buffer copy}}

{{Read buffer data back}}

{{Release buffers}}
```

## Creating a buffer

The same creation idiom occurs here, first a descriptor then a call to `createBuffer`:

```Cpp
BufferDescriptor bufferDesc;
bufferDesc.label = "Some GPU-side data buffer";
bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
bufferDesc.size = 16;
bufferDesc.mappedAtCreation = false;
Buffer buffer1 = device.createBuffer(bufferDesc);
```

One notable difference with a CPU buffer is that we must state some usage hints, telling about our use of this memory. For instance, if we are going to use it only to write it from CPU but never to read it back, we set its `CopyDst` usage flag on but not the `CopySrc` flag. This is not fully agnostic memory management helps the device figure out the best memory layout.

>Note
> A GPU buffer is mapped when it is connected to a specific part of the CPU-side RAM. The driver then automatically synchronizes its content, either for reading or for writing. We do not use this feature for now.

For our little exercise create a second buffer, called `buffer2`. We will load data in the first buffer, issue a copy command so that the GPU copies data from one to another, then read the destination buffer back.

We can reuse the descriptor, only changing the label for now:

```Cpp
bufferDesc.label = "Output buffer";
Buffer buffer2 = device.createBuffer(bufferDesc);
```

Also, don't forget to release your buffers once you no longer use them:

```Cpp
// In Terminate()
buffer1.release();
buffer2.release();
```

>Note
> Buffers, and textures provide a destroy method:
>```Cpp
>buffer1.destroy();
>```
> This can be used to force freeing the GPU memory even if there remains existing references to the buffer.
>* Destroy frees the GPU memory that was allocated for the buffer, but the buffer object itself, which lives on the driver/backend side, still exists.
>* Release frees the driver/backend side object(or rather dereferences its reference pointer) and destroys it if nobody else uses it.

## Writing to a buffer

The device provides a `queue.writeBuffer` function(or the C-style `wgpuQueueWriteBuffer`), to which we first give the GPU buffer to write into, then a CPU-side memory address and size from which data is copied:

```Cpp
// Create some CPU-side data buffer (of size 16 bytes)
std::vector<uint8_t> numbers(16);
for (uint8_t i = 0; i < 16; ++i) numbers[i] = i;
// `numbers` now contains [ 0, 1, 2, ... ]

// Copy this from `numbers` (RAM) to `buffer1` (VRAM)
queue.writeBuffer(buffer1, 0, numbers.data(), numbers.size());
```

> Note
> Uploading data from the CPU-side memory(RAM) to the GPU-side memory(VRAM) takes time. When the function `writeBuffer()` returns, data transfer may not have finished yet but what is guaranteed is that:
>* You can free up the memory from the address you just passed, because the backend maintains its own CPU-side copy of the buffer during transfer(use mapping if you want to avoid that).
>* Commands that are submitted in the queue after the `writeBuffer()` operation will not be executed before the data transfer is finished.
>And don't forget that commands sent through the command encoder are only submitted when calling `queue.submit()` with the encoded command buffer returned by `encoder.finish()`.

## Copying a buffer

We can now submit a buffer-buffer copy operation to the command queue. This is not directly available from the queue object but rather requires to create a command encoder. We may use the same one as the render pass for our test and simply add the following:

```Cpp
// After creating the command encoder
encoder.copyBufferToBuffer(buffer1, 0, buffer2, 0, 16);
```

The argument `0` after each buffer is the byte offset within the buffer at which the copy must happen. This enables copying sub-parts of buffers.

We wrap this in a command encoding process similar to the render pass one:

```Cpp
CommandEncoder encoder = device.createCommandEncoder(Default);

{{Copy buffer to buffer}}

CommandBuffer command = encoder.finish(Default);
encoder.release();
queue.submit(1, &command);
command.release();
```

## Reading from a buffer

The command queue, that we used to send data (`writeBuffer`) and instructions (`copyBufferToBuffer`), only goes in one way: from CPU host to GPU device. It is thus a "fire and forget" queue: functions do not return a value since they run on a different timeline.

So, how do we read data back then? We use an asynchronous operation, like we did when using `wgpuQueueOnSubmittedWorkDone` in the Command Queue chapter. Instead of directly get a value back, we setup a callback that gets invoked whenever the requested data is ready. We then poll the device to check for incoming events.

To read data from a buffer, we use `buffer.mapAsync`(or `wgpuBufferMapAsync`). This operation maps GPU buffer into CPU memory, and then whenever it is ready it executes the callback function it was provided. Once we are done, we can unmap the buffer.

>Note
> This asynchronocity makes the programming workflow more complicated than synchrnous operations, but it is very important to minimize wasteful processor idling. It is common to launch a mapping operation, then do other things while waiting for the data(which takes a lot of time, compared to running CPU instructions).

### Mapping

Let us first change the `usage` of the second buffer by adding the `BufferUsage::MapRead` flag, so that the buffer can be mapped for reading:

```Cpp
bufferDesc.label = "Output buffer";
bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
Buffer buffer2 = device.createBuffer(bufferDesc);
```

> Note
> The `BufferUsage::MapRead` flag is not compatible with `BufferUsage::CopySrc` one, so make sure not to have both at the same time. It is common to create a buffer dedicated to mapping operations.

We can now call the buffer mapping with a simple callback. The `wgpuBufferMapAsync` procedure takes as argument the map mode(read, write or both), the slice buffer data to map, given by an offset(0) and a number of bytes(16), then callback and finally some "user data" pointer. We show below what the latter is for.

```Cpp
auto onBuffer2Mapped = [](WGPUBufferMapAsyncStatus status, void* /* pUserData */) {
    std::cout << "Buffer 2 mapped with status " << status << std::endl;
};
wgpuBufferMapAsync(buffer2, MapMode::Read, 0, 16, onBuffer2Mapped, nullptr /*pUserData*/);
```

> Important
> The C-style procedure is used intentionally her for now. It helps with understanding what is happening under the C++ syntactic sugar of the wrapper.

### Asynchronous polling

If you run the program at this point, you might be surprised(and disappointed) to see that the callback is never executed! We saw this in Device Polling section of the command queue chapter: there is no hidden process executed by the webGPU library to check that the async operation is ready so we must do it ourselves.

Unfortunately, this mechanism has not standard solution yet, so we write it differently for `dawn`, `wgpu-native`, and `emscripten`:

```Cpp
// We define a function that hides implementation-specific variants of device polling:
void wgpuPollEvents([[maybe_unused]] Device device, [[maybe_unused]] bool yieldToWebBrowser) {
#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
    if (yieldToWebBrowser) {
        emscripten_sleep(100);
    }
#endif
}
```

```Cpp
{{Define the wgpuPollEvents function}}
```

> Emscripten Subtlety
>When our C++ code runs in a Web browser (after being compiled to WebAssembly through emscripten), there is no explicit way to tick/poll the WebGPU device. This is because the device is managed by the Web browser itself, which decides at what pace polling should happen. As a result:
>* The device never ticks in between two consecutive lines of our WebAssembly module, it can only tick when the execution flow leaves the module.
>* The device always ticks between two calls to our MainLoop() function, because if you remember the Emscripten section of the Opening a Window chapter, we leave the main loop management to the browser and only provide a callback to run at each frame.
>Thanks to the second point, we do not need wgpuPollEvents to do anything when called at the beginning or end of our main loop (so we set yieldToWebBrowser to false).
>However, if what we intend is really to wait until something happens (e.g., a callback gets invoked), the first point requires us to make sure we yield back the execution flow to the Web browser, so that it may tick its device from time to time. We do this thanks to emscripten_sleep function, at the cost of effectively sleeping during 100 ms (we’re in a case where we want to wait anyways).
>Note that using emscripten_sleep requires the -SASYNCIFY link option to be passed to emscripten, like we added already.

In our example, we want to wait for read back during an iteration of the main loop, so we specify that we yield back to the browser:

```Cpp
bool ready = false;

{{Define callback and start mapping buffer}}

while (!ready) {
    wgpuPollEvents(device, true /* yieldToBrowser */);
}
```

You could now see `Buffer 2 mapped with status 0`(0 being the value of `BufferMapAsyncStatus::Success`) when running your program. However, we never change the `ready` variable to `true`! So the program then halts forever... not great. That is why the next section shows how to pass some context to the callback.

### Mapping Context

So we need the callback to access and mutat the `ready` variable. But how can we do this since `onBuffer2Mapped` is a seperate function whose signature cannot be changed? We can use the user pointer, like we did in the adapter request or the device request.

>Note
> When defining `onBuffer2Mapped` as a regular function, it is clear that `ready` is not accessible. When using the lambda expression like we did above, one could be tempted to add `ready` in the capture list(the brackets before function arguments). But this does not work because a capturing lambda has a different type, that cannot be used as a regular callback. We see below that the C++ wrapper fixes this limitation.

```Cpp
// A first use of the 'pUserData' argument.
bool ready = false;

auto onBuffer2Mapped = [](WGPUBufferMapAsyncStatus status, void* pUserData) {
    // We know by convention with ourselves that the user data is a pointer to 'ready':
    bool* pReady = reinterpret_cast<bool*>(pUserData);
    // We set ready to 'true'
    *pReady = true;

    std::cout << "Buffer 2 mapped with status " << status << std::endl;
};

wgpuBufferMapAsync(buffer2, MapMode::Read, 0, 16, onBuffer2Mapped, (void*)&ready);
//                                Pass the address of 'ready' here: ^^^^^^^^^^^^
```

Now we need to access to more than a status when running this callback, since we need to access the buffer's content. But the `onBuffer2Mapped` function cannot have a second user pointer! Not a problem: we can define a context structure, that holds all the fields that we want to share with the callback, then pass the address on an instance of this context.

```Cpp
// The context shared between this main function and the callback.
struct Context {
    bool ready;
    Buffer buffer;
};

auto onBuffer2Mapped = [](WGPUBufferMapAsyncStatus status, void* pUserData) {
    Context* context = reinterpret_cast<Context*>(pUserData);
    context->ready = true;
    std::cout << "Buffer 2 mapped with status " << status << std::endl;
    if (status != BufferMapAsyncStatus::Success) return;

    {{Use context->buffer here}}
};

// Create the Context instance
Context context = { false, buffer2 };

wgpuBufferMapAsync(buffer2, MapMode::Read, 0, 16, onBuffer2Mapped, (void*)&context);
//                   Pass the address of the Context instance here: ^^^^^^^^^^^^^^

while (!context.ready) {
    //  ^^^^^^^^^^^^^ Use context.ready here instead of ready
    wgpuPollEvents(device, true /* yieldToBrowser */);
}
```

>Tip
> When the whole operation lives in a class like our `Application`, it can be convenient to simply use `this` as the user pointer and thus retrieve the whole application object inside the callback.

## Using the Mapped Buffer

Once the buffer is mapped(either directly within the callback or after the polling loop that makes sure it is ready), we use `Buffer::getConstMappedRange`(aka `wgpuBufferGetConstMappedRange`) to get a pointer to the CPU-side data. Once we are done with this CPU-side data, we must unmap the buffer:

```Cpp
// Get a pointer to wherever the driver mapped the GPU memory to the RAM
uint8_t* bufferData = (uint8_t*)context->buffer.getConstMappedRange(0, 16);

{{Do stuff with bufferData}}

// Then do not forget to unmap the memory
context->buffer.unmap();
```

>Note
> When mapping the buffer in write mode, use `Buffer::getMappedRange` (aka `wgpuBufferGetMappedRange`) instead of the "const" version.

For instance we can just display the content of the buffer and check that it corresponds to our initially fed buffer data:

```Cpp
std::cout << "bufferData = [";
for (int i = 0; i < 16; ++i) {
    if (i > 0) std::cout << ", ";
    std::cout << (int)bufferData[i];
}
std::cout << "]" << std::endl;
```

# Conclusion

Congratulations! We are able to create a GPU-side memory buffer, upload data into it, copy it remotely(operation triggered from the CPU, executed on the GPU) using the command queue and download data back from the GPU. We can now use a buffer to specify vertex attributes, in particular vertex postions!


