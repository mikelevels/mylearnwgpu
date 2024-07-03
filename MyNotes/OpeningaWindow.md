
Step020

# Opening a Window

Before being able to render anything on screen, we need to ask the OS to hand us some place to draw things, something commonly known as a window.

The process to open a window depends a lot on the OS. GLFW is a library that unifies the different window management APIs and enables our code to be agnostic in the OS.

> Note
> More often than not this tutorial avoids the use of many libraries. GLFW is required to make our code cross-platform. GLFW is a very common choice and is minimal.

> Headless mode
> WebGPU does not require a window to draw things actually, it may run headless and draw to offscreen textures. Since this is not a use case as commom as drawing in a window, the details are located in a dedicated chapter in the advanced section.

> SDL
> Another popular choice for window management is the SDL library. It is not as lightweight as GLFW but provides more features, like support for sound and Androiud/iOS targets. There is a dedicated section of the appendix just for integrating SDL.

## Installation of GLFW

Add the code of GLFW to your project. Download and unzip it in your project directory.

Dont forget to add it to your build system as a dependency and some stuff about the target outcome.

> NOTE: you added quite a bit more with another directory as an include among other items. Carefully review the differences between commits here.

> Important: If you are on Linux there are additional dependencies for GLFW. By default your build system tries to build for both X11 and Wayland so you need both sets of dependencies. If you only want to use/install one of them, turn either GLFW_BUILD_X11 or GLFW_BUILD_WAYLAND off when calling cmake, for example:
>```Cpp
>cmake -B build -DGLFW_BUILD_WAYLAND=OFF
>```

## Basic Usage

### Initialization

Any call to GLFW must be between its initialization and termination:

```Cpp
glfwInit();
{{Use GLFW}}
glfwTerminate();
```

The init function returns false when it could not set things up properly:

```Cpp
if (!glfwInit()) {
    std::cerr << "Could not initialize GLFW!" << std::endl;
    return 1;
}
```

Once the library has been initialized, it is possible to create a window:

```Cpp
// Create the window
GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

{{Use the window}}

// At the end of the program, destroy the window
glfwDestroyWindow(window);
```

Here again, we may add some error management:

```Cpp
if (!window) {
    std::cerr << "Could not open window!" << std::endl;
    glfwTerminate();
    return 1;
}
```

### Window hints

The `glfwCreateWindow` function has some optional extra arguments that are passed through calls of `glfwWindowHint` before invoking `glfwCreateWindow`. We add two hints to our case:

* Setting `GLFW_CLIENT_API` to `GLFW_NO_API` tells GLFW not to care about the graphics API, as it does not know WebGPU and we won't use what it could set up by default for other APIs.

* Setting `GLFW_RESIZABLE` to `GLFW_FALSE` prevents the user from resizing the window. We will release this constraint later on, but for now it avoids some inconvenient crash.

```Cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // <-- extra info for glfwCreateWindow
glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);
```

> Tip
> Please take a look at the documentation of GLFW to know more about `glfwCreateWindow` and other related functions.

### Main loop

At this point, the window opens and closes immediately after. Add the applications' main loop just before all the release/destroy/terminate calls:

```Cpp
while (!glfwWindowShouldClose(window)) {
    // Check whether the user clicked on the close button (and any other
    // mouse/key event, which we don't use so far)
    glfwPollEvents();
}
```

> Note
> This main loop is where most of the applications' logic occurs. We will repeatedly clear and redraw the whole image, and check for new user input.

> Warning
> The main loop described above will not work with Emscripten. In a web page, the main loop is handled by the browser and we just tell it what to call at each frame. We fix this issue below, and refactor the program around an `Application` class.

# Application class

## Refactor

Reorganize the project a bit for web friendly-ness and to clarify about the initialization stage of the application and the main loop seperation.

We create functions `Initialize()`, `MainLoop()`, and `Terminate()` to split up the three key parts of our program. We also put all the variables that these functions share in a common class/struct called `Application`. For better readability, we make `Initialize()`, `MainLoop()`, and `Terminate()` members of the class:

```Cpp
class Application {
public:
    // Initialize everything and return true if it went all right
    bool Initialize();

    // Uninitialize everything that was initialized
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Return true as long as the main loop should keep on running
    bool IsRunning();

private:
    // We put here all the variables that are shared between init and main loop
    {{Application attributes}}
};
```

The new structure of the file main.cpp:

```Cpp
{{Includes}}

{{Application class}}

{{Main function}}

{{Application implementation}}
```

I personally prefer to move the implementation details of the class application as well as its interface to its own files. However I will chose to follow the tutorial as is for now.

This changes allow our main function to be as simple as:

```Cpp
int main() {
    Application app;

    if (!app.Initialize()) {
        return 1;
    }

    // Warning: this is still not Emscripten-friendly, see below
    while (app.IsRunning()) {
        app.MainLoop();
    }

    return 0;
}
```

Now simply move all of the initialization code to the appropriate function, all of the termination code to the terminate function, etc. The only thing located in mainloop is to poll for GLFW events and webgpu device.

> Important
> Do not move the `emscripten_sleep(100)` line to `MainLoop()`. This line is no longer needed once we let the browser handle the main loop, the browser ticks its WebGPU backend itself.

Once everything has been moved over, there are only a few private items in the class.

> Dont forget
> The `WGPUInstance` and `WGPUAdapter` are intermediate steps towards getting the device that may be released in the initialization.

## Emscripten

As mentioned above, writing the while loop is not possible when building for the Web (with Emscripten) because it conflicts with the browsers' own loop. We thus write the main loop differently in such a case:

```Cpp
#ifdef __EMSCRIPTEN__
    {{Emscripten main loop}}
#else // __EMSCRIPTEN__
    while (app.IsRunning()) {
        app.MainLoop();
    }
#endif // __EMSCRIPTEN__
```

`main` is now a little different:

```Cpp
int main() {
    Application app;

    if (!app.Initialize()) {
        return 1;
    }

    {{Main loop}}

    return 0;
}
```

Here we use the function `emscripten_set_main_loop_arg()`, which is precisely dedicated to this issue. This sets a callback that the browser will call each time it runs its main rendering loop.

```Cpp
// Callback type takes one argument of type 'void*' and returns nothing
typedef void (*em_arg_callback_func)(void*);

// Signature of 'emscripten_set_main_loop_arg' as provided in emscripten.h
void emscripten_set_main_loop_arg(
    em_arg_callback_func func,
    void *arg,
    int fps,
    int simulate_infinite_loop
)
```

We can recognize the callback pattern that we used already when requesting the adapter and device, or setting error callbacks. What is called `arg` here is what WebGPU calls `userdata`: it is a pointer that is blindly passed to the callback function.

```Cpp
// Equivalent of the main loop when using Emscripten:
auto callback = [](void *arg) {
    //                   ^^^ 2. We get the address of the app in the callback.
    Application* pApp = reinterpret_cast<Application*>(arg);
    //                  ^^^^^^^^^^^^^^^^ 3. We force this address to be interpreted
    //                                      as a pointer to an Application object.
    pApp->MainLoop(); // 4. We can use the application object
};
emscripten_set_main_loop_arg(callback, &app, 0, true);
//                                     ^^^^ 1. We pass the address of our application object.
```

The extra arguments are recommended to be `0` and `true`:
* `fps` is the framerate at which the function gets called. For better performance, it is recommended to set it to 0 to leave it up to the browser(equivalent of using `requestAnimationFrame` in JavaScript)
* `simulate_infinite_loop` must be `true` to prevent `app` from being freed. Otherwise, the `main` function returns before the callback gets invoked, so the application no longer exists and the `arg` pointer is dangling(POINTS TO NOTHING VALID).

# The Surface

Remember the surface is an object that is drawn to. Connect the GLFW window to WebGPU. This happens when requesting the adapter, by specifying a WGPUSurface object to draw on:

```Cpp
{{Get the surface}}

WGPURequestAdapterOptions adapterOpts = {};
adapterOpts.nextInChain = nullptr;
adapterOpts.compatibleSurface = surface;
//                              ^^^^^^^ Use the surface here

WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);
```

How do we get the surface? Depends on the OS and GLFW does not handle this for us, because it doesnt know WebGPU yet. So I provide you this function, it is in an extension to GLFW3 called `glfw3webgpu`.

## GLFW3 WebPGU Extension

> Note: You made a comment before
> Earlier you stated you needed to add extra stuff to the directory in addition to the lean version of GLFW. It is true that is what you did, HOWEVER this is the section of the tutorial where Elie Michel added it to this project. Here he calls for you to download and install `glfw3webgpu.zip` in your projects directory as well as make all the necessary build file changes as well. You did this already. BUT you opted to continue following the tutorial as is for completeness.

Download and unzip `glfw3webgpu.zip` in your project's directory. As before add the directory and link the target it creates to our app.

> Note
> `glfw3webgpu` library is very simple it is only made of two files. Instead of including it directly in the source tree there are special compilation flags in macOS. Check the `CMakeLists.txt` file to see them for yourself.

Get the surface:

```Cpp
#include <glfw3webgpu.h>
//...
surface = glfwGetWGPUSurface(instance, window);
```

Dont forget to release the surface at the end:

```Cpp
wgpuSurfaceRelease(surface);
```

> Important
> The surface lives independently from the adapter and device, so it must not be released before the end of the program like for the adapter and instance. Therefore it is a class attribute of the application class.

# Conclusion

The following occurred in this chapter:
* Use the GLFW library to handle windowing, and user input(details later).
* Refactor our code to seperate initilization from main loop
* Connect WebGPU to our window using `glfw3webgpu` extension.

Moving on to displaying something on this window!

Step020
