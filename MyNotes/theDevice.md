
Step010

# The Device

A WebGPU device represents a context of use of the API. All the graphical objects created are owned by the device.

The device is requested from an adapter by specifying the subset of limits and features that we are interested in. Once the device is created, the adapter should no longer be used. The only capabilities that matter to the application are the one of the device.

## Device request

Requesting the device looks a lot like requesting the adapter, so we will use a similar function:

SEE wepgpu-utils.cpp

In the main function after getting the adapter, we can request the device.

The device is released when the program ends. See bottom of main.cpp.

The adapter can be released before the device. It is good practice to release it as soon as we have the device and never use the adapter again.

## Device descriptor

Check out `webgpu.h` specifically at what the descriptor looks like.

```Cpp
typedef struct WGPUDeviceDescriptor {
    WGPUChainedStruct const * nextInChain;
    WGPU_NULLABLE char const * label;
    size_t requiredFeatureCount;
    WGPUFeatureName const * requiredFeatures;
    WGPU_NULLABLE WGPURequiredLimits const * requiredLimits;
    WGPUQueueDescriptor defaultQueue;
    WGPUDeviceLostCallback deviceLostCallback;
    void * deviceLostUserdata;
} WGPUDeviceDescriptor;

// (this struct definition is actually above)
typedef struct WGPUQueueDescriptor {
    WGPUChainedStruct const * nextInChain;
    WGPU_NULLABLE char const * label;
} WGPUQueueDescriptor;
```
For now, the tutorial implementation initializes everthing to a very minimal option, no special features, and using the default limits.

SEE webgpu-utils.cpp

The options will be refined further depending on the needs of the tutorial section.

## Inspecting the device

Like the adapter, the device has its own set of capabilities.

> Implementation divergences
> Like for wgpuAdapterGetLimits, the procedure wgpuDeviceGetLimits returns a boolean in wgpu-native but a WGPUStatus in Dawn.

It is clear from the console output that by default the device limits are not the same as what the adapter supports. Setting deviceDesc.requiredLimits to nullptr above correspond to ask for minimal limits.

## Device callbacks

In order to get notified when the device undergoes an error or when it is no longer available, we may setup callback functions. These help with debugging, it is recommended to do so.

### Device Lost Callback

As seen above, the device lost callback is provided through the device descriptor's `deviceLostCallback` field.

The device is always "lost" when it is destroyed by the ultimate call to `wgpuDeviceRelease`. It may also be lost for other reasons, mostly meaning that the backend implementation panicked and crashed.

> Important
> The `deviceLostCallback` must outlive the device, so that when the latter gets destroyed the callback is still valid.

## Uncaptured Error Callback

This callback is invoked whenever we misuse the API, and gives very informative feedback about what went wrong. It only set after the creation of the device, by calling `wgpuDeviceSetUncapturedErrorCallback`.

If you are using a debugger it is recommended to put a breakpoint in this callback so that the program pauses and provides you with a call stack whenever webgpu encounters an unexpected error.

> Dawn
> By default Dawn runs callbacks only when the device "ticks", so the error callbacks are invoked in a different call stack than where the error occurred. This makes debugging harder. Force Dawn to invoke error callbacks as soon as there is an error, you can enable an instance toggle: SEE webgpu-utils.cpp
> Toggles are Dawn's special way of enabling/disabling features at the scale of the whole WebGPU instance. The whole list is in `Toggle.cpp`.

# Conclusion

* The device is used to create all other WebGPU objects.
* Important: Once the device is created, the adapter should no longer be used. The only capabilities that matter to the application are the ones of the device.
* Default limits are minimal limits, not what the adapter supports. This helps ensuring consistency across devices.

Step010
