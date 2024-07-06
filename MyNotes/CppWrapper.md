
Step028

# C++ Wrapper

> CAUTION
> This chapter is not as up-to-date as the readme file of WebGPU-C++. Read that as well. The link:
> https://github.com/eliemichel/WebGPU-Cpp

So far we have used the raw WebGPU API, it is a C API, this prevented the use of some nice productivity features of C++. This chapter provides some examples and links to a little library that implements these features in an alternate `webgpu.hpp` replacing `webgpu.h`.

The remainder of the guide always provides snippets with both the C++ wrapper and the "vanilla" C API.

> IMPORTANT
> All the changes presented only affect the coding time, but the shallow C++ wrapper leads to the same runtime binaries.

> Note
> Dawn and emscripten provide their own C++ wrapper. They follow a similar spirit as the one introduced here, the wrapper made below was designed to work for all possible implementations, including `wgpu-native`

# Namespace

Recall namespaces do not exist in C, which explains why every single function starts with wgpu and every single structure starts with WGPU. A more C++ idiomatic way of doing this is to enclose all these functions into a namespace.

```Cpp
// Using Vanilla webgpu.h
WGPUInstanceDescriptor desc = {};
WGPUInstance instance = wgpuCreateInstance(&desc);
```

becomes the following with namespaces:

```Cpp
// Using C++ webgpu.hpp
wgpu::InstanceDescriptor desc = {};
wgpu::Instance instance = wgpu::createInstance(&desc);
```

After all that is setup then you could take things a step further and simply preamble with `using namespace wgpu;` moving forward.(I DONT DO THIS BECAUSE IT GOES AGAINST MY PROGRAMMING BEST PRACTICES.)

# Objects

Beyond namespace, most functions are also prefixed by the type of their first argument, for instance:

```Cpp
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, WGPUBufferDescriptor const * descriptor);
               ^^^^^^             ^^^^^^^^^^^^^^^^^
size_t wgpuAdapterEnumerateFeatures(WGPUAdapter adapter, WGPUFeatureName * features);
           ^^^^^^^                  ^^^^^^^^^^^^^^^^^^^
void wgpuBufferDestroy(WGPUBuffer buffer);
         ^^^^^^        ^^^^^^^^^^^^^^^^^
```

These functions are conceptually methods of the object constituted by their first argument. Once again C does not have built-in support for methods C++ does, so in the wrapper we expose these WebGPU functions as follows:

```Cpp
namespace wgpu {
    struct Device {
        // [...]
        createBuffer(BufferDescriptor const * descriptor = nullptr) const;
    };

    struct Adapter {
        // [...]
        enumerateFeatures(WGPUFeatureName * features) const;
    };

    struct Device {
        // [...]
        destroy();
    };
} // namespace wgpu
```

> Note
> The `const` qualifier is specified for some methods. This is extra information provided by the wrapper to reduce the potential programming mistakes.

This greatly reduces visual clutter when calling such methods:

```Cpp
// Using Vanilla webgpu.h
WGPUAdapter adapter = /* [...] */
size_t n = wgpuAdapterEnumerateFeatures(adapter, nullptr);
```

becomes with namespaces:

```Cpp
// Using C++ webgpu.hpp
Adapter adapter = /* [...] */
size_t n = adapter.enumerateFeatures(nullptr);
```

# Scoped enumerations

Enums are unscoped by default, the C API is forced to prefix all values that an enumeration can take with the name of the enum, leading to some very long names:

```Cpp
typedef enum WGPURequestAdapterStatus {
    WGPURequestAdapterStatus_Success = 0x00000000,
    WGPURequestAdapterStatus_Unavailable = 0x00000001,
    WGPURequestAdapterStatus_Error = 0x00000002,
    WGPURequestAdapterStatus_Unknown = 0x00000003,
    WGPURequestAdapterStatus_Force32 = 0x7FFFFFFF
} WGPURequestAdapterStatus;
```

It is possible in C++ to define scoped enums, which are strongly typed and can only be accessed through the name, for instance this scoped enum:

```Cpp
enum class RequestAdapterStatus {
    Success = 0x00000000,
    Unavailable = 0x00000001,
    Error = 0x00000002,
    Unknown = 0x00000003,
    Force32 = 0x7FFFFFFF
};
```

This can be used as follows:

```Cpp
wgpu::RequestAdapterStatus::Success;
```

> Note
> The actual implementation use a little trickery so that enum names are scoped, but implicitly converted to and from the original WebFGPU enum values.

# Default descriptor values

Sometimes we just need to build a descriptor by default. More generally, we rarely need to have all the fields of the descriptor deviate from the default, so we could benefit from the possibility to have a default contructor for descriptors.

# Capturing closures

Many asynchronous operations use callbacks. In order to provides some context to the callback's body, there is always a `void * userdata` argument passed around. This can be alleviated in C++ by using capturing closures.

> Important
> This only alleviates the notations, but technically mechanism very similar to the user data pointer is automatically implemented when creating a capturing lambda.

```Cpp
// C style
struct Context {
    WGPUBuffer buffer;
};
auto onBufferMapped = [](WGPUBufferMapAsyncStatus status, void* pUserData) {
    Context* context = reinterpret_cast<Context*>(pUserData);
    std::cout << "Buffer mapped with status " << status << std::endl;
    unsigned char* bufferData = (unsigned char*)wgpuBufferGetMappedRange(context->buffer, 0, 16);
    std::cout << "bufferData[0] = " << (int)bufferData[0] << std::endl;
    wgpuBufferUnmap(context->buffer);
};
Context context;
wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, 16, onBufferMapped, (void*)&context);
```

becomes:

```Cpp
// C++ style
buffer.mapAsync(buffer, [&context](wgpu::BufferMapAsyncStatus status) {
    std::cout << "Buffer mapped with status " << status << std::endl;
    unsigned char* bufferData = (unsigned char*)context.buffer.getMappedRange(0, 16);
    std::cout << "bufferData[0] = " << (int)bufferData[0] << std::endl;
    context.buffer.unmap();
});
```

# Library

The library providing these C++ idioms is `webgpu.hpp`. It consists of a single header file, you only need to copy into your source tree. One of your source files needs to define `WEBGPU_CPP_IMPLEMENTATION` before `#include <webgpu/webgpu.hpp>`:

```Cpp
#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
```

> Note
> This header is actually included in the webgpu zip provided before, at the include path webgpu/webgpu.hpp

More information can be found in the repository: https://github.com/eliemichel/WebGPU-Cpp

AND THEN THERE WERE MANY CODE BLOCKS HERE. I didnt add them here you can see them in the webbook if you wish.


