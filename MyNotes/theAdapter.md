# The Adapter

See code in step005

Before initializing a device, we need to choose an adapter. Some host systems may have multiple adapters if it has access to multiple physical GPUs. It may also have an adapter that represents an emulated/virtual device.

> NOTE:
> Some high-end laptops feature two physical GPUs, a high performance one and a low energy consumption one typically integrated into the CPU chip.

Each adapter advertises a list of optional features and supported limits that it can handle. These are used to determine the overall capabilities of the system before requesting the device.

| Why is there an adapter and a device abstraction? |

The idea is to limit the "it worked on my machine issue" you could encounter when trying your program on a different machine. The *adapter* is used to *access the capabilities* of the hardware. Then you can figure out the behavior of your application among different code paths. Once a code path is chosen, a *device* is created with *the capabilities we choose*.

Only the capabilities selected for the device are allowed in the rest of the application. This way it is *not possible to accidentally rely on capabilities specific to your machine*.

In an advanced setup of the adapter/device facility, it is possible to setup multiple presets and select one depending on the adapter. For the sake of this tutorial we are using a single preset. The application closes early if the preset is not supported.

# Requesting the adapter

An adapter is not something we create, it is an item we request from a function: wgpuInstanceRequestAdapter.

As suggested by the name, the first argument is the WGPUInstance that we created in the previous chapter. What about others?

```Cpp
//Signature of the wgpuInstanceRequesterAdapter function located in webgpu.h
void wgpuInstanceRequestAdapter(
    WGPUInstance instance,
    WGPU_NULLABLE WGPURequestAdapterOptions const* options,
    WGPURequestAdapterCallback callback,
    void* userdata
)
```

> NOTE: When you have time look at the contents of webgpu.h!

The second option is the set of options, it acts a lot like an descriptor in `wgpuCreateSomething` functions. The `WGPU_NULLABLE` flag is an empty define. It is there to tell us we can set it to a `nullptr` so we can use *default options*.

# Asynchronous function

The last two arguments in the function stub above go together, this is another WebGPU idiom. Indeed, the function `wgpuInstanceRequestAdapter` is asynchronous. This means that instead of directly returning a `WGPUAdapter` object, this request function remembers a "callback", ie a function that will be called whenever the request ends.

> NOTE: Async functions are used in multiple places in the WebGPU API, whenever an operation may take time. Actually, non of the webGPU functions takes tim eto return. The program we are writing never gets blocked by a lengthy operation.

Here is the definition of the `WGPURequestAdapterCallback` function type:

```Cpp
// Definition of the WGPURequestAdapterCallback function type as defined in webgpu.h
typedef void (*WGPURequestAdapterCallback)(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    char const * message,
    void * userdata
);
```
The callback is a function that receives the requested adapter as an argument, together with status information (that tells whether the request failed and why), as well as this mysterious `userdata` pointer.

`userdata` pointer can be anything it is not interpreted by WebGPU, it is only forwarded from the first call to `wgpuInstanceRequestAdapter` to the callback, as a means to share some context information:

```Cpp
void onAdapterRequestEnded(
    WGPURequestAdapterStatus status, // a success status
    WGPUAdapter adapter, // the returned adapter
    char const* message, // error message, or nullptr
    void* userdata // custom user data, as provided when requesting the adapter
) {
    // [...] Do something with the adapter

    // Manipulate user data
    bool* pRequestEnded = reinterpret_cast<bool*>(userdata);
    *pRequestEnded = true;
}

// [...]

// In main():
bool requestEnded = false;
wgpuInstanceRequestAdapter(
    instance /* equivalent of navigator.gpu */,
    &options,
    onAdapterRequestEnded,
    &requestEnded // custom user data is simply a pointer to a boolean in this case
);
```

In the next section we see a more advanced use case of this context in order to receive the adapter once the request is done.

> NOTE: Javascript API
> In the JavaScript API of WebGPU, asynchronous functions use the built-in [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) mechanism:
```js
const adapterPromise = navigator.gpu.requestAdapter(options);
// The "promise" has no value yet, it is rather a handle that we may connect to callbacks:
adapterPromise.then(onAdapterRequestEnded).catch(onAdapterRequestFailed);

// [...]

// Instead of a 'status' argument, we have multiple callbacks:
function onAdapterRequestEnded(adapter) {
	// do something with the adapter
}
function onAdapterRequestFailed(error) {
	// display the error message
}
```
> The JavaScript language later introduced a mechanism `async` function which enables "awaiting" for an asynchronous function without explicitly creating a callback:
```js
// (From within an async function)
const adapter = await navigator.gpu.requestAdapter(options);
// do something with the adapter
```
>This mechanism now exists in other languages such as Python, and has even been introduced in C++20 with coroutines.
>Due to this guide using C++17, we are not using coroutines. When you have time try to implement all of the same things using C++20 coroutines.

# Request

The entire adapter request can be wrapped in a single function `requestAdapterSync()`. Notably, this approach avoids boilerplate code. You need to spend some time setting up a means of capturing errors when you have time.

In the main function after opening the window we can get the adapter.

## Waiting for the request to end

Note the comment in the code stating that we need to wait for the request to end, for the callback to be invoked, before returning.

When using the native API(Dawn or wgpu-native), it is in practive not needed, we know that when `wgpuInstanceRequestAdapter` function returns its callback has been called.

However, when using Emscripten, we need to hand the control *back to the browser* until the adapter is ready. In JavaScript, this would be using the `await` keyword. Instead, Emscripten provides the `emscripten_sleep` function that interrupts the C++ module for a couple of milliseconds:

```Cpp
#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded) {
        emscripten_sleep(100);
    }
#endif // __EMSCRIPTEN__
```

In order for the new application build to handle this we need to add a *custom link option* in `CMakeLists.txt`, in the `if(EMSCRIPTEN)` block:

```CMake
# Enable the use of emscripten_sleep()
target_link_options(App PRIVATE -sASYNCIFY)
```

Additionally, the use of preprocessor directives, and possible header file order is important. You spent some time trying to make things work but ultimately found the updated CMakeList.txt file on the github repo that is best used in this case.

```Cpp
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif // __EMSCRIPTEN__
```

# Destruction

Like for the WebGPU instance, we must release the adapter:

```Cpp
wgpuAdapterRelease(adapter);
```

> NOTE:
> The WebGPU instance is only required for a limited time, it is not needed once we have our adapter. So it is okay to release the instance after the adapter request instead of at the very end. The underlying instance object will keep on living until the adapter gets released but we do not need to manage the object at this point any longer. See the sample High-Level code blocks:

```Cpp
// [...] Create WebGPU instance
// [...] Check WebGPU instance
// [...] Request adapter
// We no longer need to use the instance once we have the adapter
// [...] Destroy WebGPU instance
```
Here is a high level summary of main:

```Cpp
// [...] Includes

// [...] Utility functions in main.cpp

int main() {
    // [...] Create things

    // [...] Main body

    // [...] Destroy things

    return 0;
}
```

# Inspecting the adapter

The adapter object provides information about the underlying implementation, hardware, and available capabilities. It advertises the following information

* Limits regroup all the maximum and minimum values that may limit the behavior of the underlying GPU and its driver. A typical example is the maximum texture size. Supported limits are retrieved using `wgpuAdapterGetLimits`.

* Features are non-mandatory *extensions* of WebGPU, some adapters do and others do not support. They can be listed using wgpuAdapterEnumerateFeatures or tested individually with `wgpuAdapterHasFeature`.

* Properties are extra information about the adapter, its name, vendor, etc. Properties are retrieved using `wgpuAdapterGetProperties`.

> IMPORTANT YOU ALMOST MISSED THIS
> You forgot/didnt update CMakeLists.txt in the main directory to enable Asynchronous calls between JS and C++.
> Additionally, you have to call emrun App.html in order for your App.wasm, App.js, and App.html to open in your browser and start running.




