# Hello Web GPU

WebGPU is a render hardware interface(RHI), it is a programming library to provide a single interface for multiple underlying graphics hardware and operating system setups.

Webgpu is simply a single header file, as far as your C++ code is concerned.

The header is located: https://github.com/webgpu-native/webgpu-headers/blob/main/webgpu.h

Unlike native APIs, the WebGPU implementation is not provided by the driver, so we must explicitly provide it.

