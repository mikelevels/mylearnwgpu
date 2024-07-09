
Step000

IMPORTANT:
FIRST - run in an x64 command prompt. Hit the windows key on your keyboard, type in "x64" it should be the first CMD option that appears.
SECOND - confirm that emsdk is running/enabled in the "x64" cmd from before.
    1) Run "emcc --check"
        1) Should get an message saying that it ran sanity checks, then nothing returns.
    2) If that doesnt work navigate to the emsdk directory and confirm that emsdk is setup properly in your current "x64" window to get a meaningful output.
THIRD - Continue with tutorial. At this point(06/23/2024) in time you were here: https://eliemichel.github.io/LearnWebGPU/getting-started/project-setup.html#lit-3

Step001

https://github.com/eliemichel/LearnWebGPU-Code/blob/step001/webgpu/README.md

# Build using wgpu-native backend
cmake -B build-wgpu -DWEBGPU_BACKEND=WGPU
cmake --build build-wgpu

# Build using Dawn backend
cmake -B build-dawn -DWEBGPU_BACKEND=DAWN
cmake --build build-dawn

# Build using emscripten
emcmake cmake -B build-emscripten
cmake --build build-emscripten

Step005

The lines to build the code from Step001 are the same. HOWEVER, in order to RUN
the .wasm output by the emscripten build steps you first need to start a web
server, THEN it will launch the app/wasm in the Chrome/default web browser.

emrun .\path\to\App.html

Most likely:

emrun .\build-emscripten\App.html

In order to exit the webserver Ctr-C then respond Y at the prompt.

DO NOT FORGET TO CONFIRM THE EMSCRIPTEN BUILD STEPS ARE CORRECT IN CMAKELISTS.TXT

The other lines to 'run' the apps you are making for the other build systems are:

.\build-{buildname}\Debug\App.exe

.\build-dawn\Debug\App.exe
.\build-wgpu\Debug\App.exe

FOR SOME REASON THE STATIC WEBGPU BUILD FAILS
ADDITIONALLY EMSCRIPTEN BUILD DOES NOT RETURN DEVICE LIMITS IN BROWSER BUT
EVERYTHING ELSE WAS FINE. FOUND SOMETHING YOU MAY NEED TO USE INSTEAD CALLED
NAVIGATORGPU WHEN USING EMSCRIPTEN.

Step010

CONFIRMED All builds except wgpu-static appeared to work without issue. emscripten
did return the device limits AFTER the device structure was in memory. Not printed
during the adapter stage of initialization.

Step020

CONFIRMED All builds except wgpu-static appeared to work without issue. The only
difference was that emscripten returned a black window, not a white one. It was a
window of the correct size though! So this serves as an indication that it worked
out. Not really interested in a static build if all the other builds are working.

IMPORTANT: All of the code in the "-next" branches are actually older than the non
-suffixed branches. You need to rewrite your code to correspond with the most up to
date branches.

REWRITING EVERYTHING NOW TO REFLECT MOST RECENT BRANCHES.

Step 025

The differences in color are due to dawn and emscripten in Chrome(also just dawn)
operating in different colorspaces than wgpu-native.

After correcting all of your code to reflect the non-next suffix branches the only
other major change was the #ifndef __EMSCRIPTEN__ #endif block!!! Without this key
section the code throws an exception in the Chrome(dawn implementation) browser.

The solution was in the webbook. Michel updated the code with a commit with the 
appropriate change to resolve the issue.

NOTE the critical block is in Application::MainLoop() around line 170-ish.

Step028

CRITICAL:
Intellisense does not reflect the expected behavior of the build output and or the
code generated during the build process.

Carefully confirm the spelling of everything. Intellisense cannot be relied on for
the remainder of the project.

Step030_cpp

ADDED: Suffix "_cpp" to commits/branch note blocks. There are alternative brnaches
available with suffix "-vanilla". These alternative branches refer to the raw C API
implementation of the WGPU interface. 

Any additonal notes in this commit follow:

ALL NECESSARY BUILDS RUN AS EXPECTED.

Step031_cpp

So far everything was fine until you found some minor differences between your code
and Michel's code. Until you encountered `Default`. Turns out it was most likely
something living in wgpu::. But it wasnt appearing at all before?

Maybe you need to clean all builds before building again? Could this lead to issues
with name resolution? There is a problem with name resolution in VSCode 
intellisense, but it couldnt find a common wrapper end point in a namespace?

The issue is resolved. You are not certain if it happened due to you manually 
cleaning up it right now. The following ran without an issue:
    wgpu
    Dawn
    emscripten

The only build that does not work is wgpu-static.

NOTE: After cleaning the builds you had a hard time rebuilding Dawn. This was most
likely due to your slow internet connection. It was only resolved after cleaning 
Dawn and rebuilding it as you usually do one more time!

Step032_cpp

Okay, so there was a few issues that occurred during initialization of the 
Application object. 
    
![alt text](image.png)

