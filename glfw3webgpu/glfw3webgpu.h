#ifndef _glfw3_webgpu_h_
#define _glfw3_webgpu_h_

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get a WGPUSurface from a GLFW window.
 */
WGPUSurface glfwGetWGPUSurface(WGPUInstance instance, GLFWwindow* window);

#ifdef __cplusplus
}
#endif

#endif // _glfw3_webgpu_h_