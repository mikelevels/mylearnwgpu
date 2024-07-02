
Step015

# The Command Queue

The *command queue* is a critical concept in graphics programming.

The CPU instructs the GPU what to do by sending commands through a command queue.

## Different timelines

One important thing to keep in mind when doing graphics programming: we have two processors running simultaneously. One of them is the CPU, also referred to as the host. The other is the GPU, or the device. There are two basic rules to consider:

1) The code we write runs on the CPU, and some of it triggers operations on the GPU. The only exception are *shaders*, these run on the GPU.
2) Processors are "far away", meaning communication between the CPU and GPU takes time.

They are not too far, but for high performance applications like real time graphics, this matters. In advanced pipelines, rendering a frame may involve thousands or tens of thousands of commands running on the GPU.

As a consequence we cannot afford to send the commands one by one from the CPU and wait for a response for each one. Instead, commands intended for the GPU are batched and fired through a command queue. The GPU consumes this queue whenever it is ready, and this way processors minimize the time spent idling for their sibling to respond.

The CPU-side of your program, the C++ code you write lives in the Content timeline. The other side of the command queue is in the Queue timeline, running on the GPU.

> Note:
> There is also a Device timeline defined in WebGPUs documentation. It corresponds to the GPU operations for which our code actually waits for an immediate answer (called "synchronous" calls), but unlike the JavaScript API, it is roughly the same as the content timeline in our C++ case.

## Queue operations

Our WebGPU device has a single queue, which is used to send both commands and data. We can get it with wgpuDeviceGetQueue.

```Cpp
WGPUQueue queue = wgpuDeviceGetQueue(device);
```

The queue must be released when it is no longer in use at the end of the program.

```Cpp
// At the end
wgpuQueueRelease(queue);
```

> Note:
> Other graphics APIs allow you to build multiple queues per device, and future versions of WebGPU may in the future. One queue is enough for the sake of this tutorial.

Take a look at `webgpu.h` there are three different ways to submit work to this queue:

* `wgpuQueueSubmit`
* `wgpuQueueWriteBuffer`
* `wgpuQueueWriteTexture`

The first one only sends commands(these can get complicated), the other two send data from CPU memory(RAM), to the GPU(VRAM). This is where the delay of communication might become critical.

Additionally, there is a function called `wgpuQueueOnSubmittedWorkDone` it can be used to setup a function call occurring once the work is done. Add a lambda to make sure things happen as expected:

```Cpp
auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
    std::cout << "Queued work finished with status: " << status << std::endl;
};
wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr /* pUserData */);
```

> Note:
> The function `onQueueWorkDone` is defined as a NON-capturing lambda it could alternatively be defined as a regular function beclared before main if it has the same signature.

## Submitting commands

Submit commands using this function:

```Cpp
wgpuQueueSubmit(queue, /* number of commands */, /* pointer to the command array */);
```

This is a typical coding practice in webGPU: WebGPU is a C API so whenever it needs to receive an array of things, we first provide the array size then a pointer to the first element.

Here is an example with a single command:

```Cpp
// With a single command:
WGPUCommandBuffer command = /* [...] */;
wgpuQueueSubmit(queue, 1, &command);
wgpuCommandBufferRelease(command); // release command buffer once submitted
```

If we know at compile time("statically") the number of commands, we may use a C array(although a std::array is safer):

```Cpp
// With a statically know number of commands:
WGPUCommandBuffer commands[3];
commands[0] = /* [...] */;
commands[1] = /* [...] */;
commands[2] = /* [...] */;
wgpuQueueSubmit(queue, 3, commands);

// or, safer and avoid repeating the array size:
std::array<WGPUCommandBuffer, 3> commands;
commands[0] = /* [...] */;
commands[1] = /* [...] */;
commands[2] = /* [...] */;
wgpuQueueSubmit(queue, commands.size(), commands.data());
```

Regardless you must remember to release the command buffers once they have been submitted:

```Cpp
// Release:
for (auto cmd : commands) {
    wgpuCommandBufferRelease(cmd);
}
```

And if we need to dynamically change the size, we use a std::vector:

```Cpp
std::vector<WGPUCommandBuffer> commands;
// [...] (Allocate and fill in command buffers)
wgpuQueueSubmit(queue, commands.size(), commands.data());
```

Notably, we cannot manually create a `WGPUCommandBuffer` object. This buffer uses a special format that is left to the discretion of your driver/hardware. To build this buffer, we use a command encoder.

## Command encoder

A command encoder is created following the usual object creation idiom of WebGPU:

```Cpp
WGPUCommandEncoderDescriptor encoderDesc = {};
encoderDesc.nextInChain = nullptr;
encoderDesc.label = "My command encoder";
WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
```

The encoder can now be used to write instructions. Since we do not have any object to manipulate yet we stick with simple debug placeholder for now:

```Cpp
wgpuCommandEncoderInsertDebugMarker(encoder, "Do one thing");
wgpuCommandEncoderInsertDebugMarker(encoder, "Do another thing");
```

Generating the command from the encoder also requires an extra descriptor:

```Cpp
WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
cmdBufferDescriptor.nextInChain = nullptr;
cmdBufferDescriptor.label = "Command buffer";
WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
wgpuCommandEncoderRelease(encoder); // release encoder after it's finished

// Finally submit the command queue
std::cout << "Submitting command..." << std::endl;
wgpuQueueSubmit(queue, 1, &command);
wgpuCommandBufferRelease(command);
std::cout << "Command submitted." << std::endl;
```

## Device polling

The code described above fails when used with Dawn:

```Cpp
Submitting command...
Command submitted.
Queued work finished with status: 4
```

wgpu-native actually completes the submitted work before the device gets released.

Take a peek at `webgpu.h`, the value `4` corresponds to `WGPUQueueWorkDoneStatus_DeviceLost`. The program terminates right after submitting the commands, without waiting for it to complete, so *the device gets destroyed before* the submitted work is done!

In the case of Dawn we need to wait a little bit, and importantly to call tick/poll the device so that it updates its awaiting tasks. This is part of the API that is not standard yet, so we must adapt our implementation to the backend:

```Cpp
for (int i = 0 ; i < 5 ; ++i) {
    std::cout << "Tick/Poll device..." << std::endl;
#if defined(WEBGPU_BACKEND_DAWN)
    wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
    wgpuDevicePoll(device, false, nullptr);
#elif defined(WEBGPU_BACKEND_EMSCRIPTEN)
    emscripten_sleep(100);
#endif
}
```

wgpu-native holds non-standard functions in wgpu.h. Need to include preprocessor directives to keep them seperate from the standard webgpu.h.

```Cpp
#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU
```

Now the output looks something like this:

```Cpp
Submitting command...
Command submitted.
Tick/Poll device...
Queued work finished with status: 0
Tick/Poll device...
Tick/Poll device...
Tick/Poll device...
Tick/Poll device...
```

To avoid using an arbitrary number of ticks, we may set a context boolean to true in `onQueueWorkDone` and break the loop as soon as it is true. In our case we will quickly call this in the main application loop anyways!

# Conclusion

We have seen a few important notions in this chapter:

* The CPU and GPU live in different timelines.
* Commands are streamed from CPU to GPU through a command queue.
* Queued command buffers must be encoded using a command encoder.
* We must regularly tick/poll the device to update its awaiting tasks.

Although this section was abstract in the next section we open a graphics window and then use our queue to display something!

>Note:
> If you are only interested in compute shaders and do not need to open a window, leave the getting started section and move on to Basic Compute. Some key concepts are introduced in the basic 3D rendering part like the Playing with buffers chapter.

Step015
