cmake_minimum_required(VERSION 3.0...3.25)
project(
	LearnWebGPU # name of the project, which will also be the name of the visual studio solution if you use it
	VERSION 0.1.0 # any version number
	LANGUAGES CXX C # programming languages used by the project
)

include(utils.cmake)

# Include glfw directory, to define the 'glfw' target
if(NOT EMSCRIPTEN)
	add_subdirectory(glfw)
else()
	# Emscripted has built-in support for GLFW but requires the `-sUSE_GLFW=3` link option:
	add_library(glfw INTERFACE)
	target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

# Include webgpu directory, to define the 'webgpu' target
add_subdirectory(webgpu)

# Include the glfw/webgpu extension
add_subdirectory(glfw3webgpu)

# We specify that we want to create a target of type executable, called "App"
add_executable(App 
		main.cpp
		webgpu-utils.h
		webgpu-utils.cpp
)

# Add webgpu target as a dependency of our app
target_link_libraries(App PRIVATE webgpu glfw glfw3webgpu)

# The application's binary must find wgpu.dll or libgw.so at runtime,
# So it is automatically copied(its called WGPU_RUNTIME_LIB in general)
# next to the binary.
target_copy_webgpu_binaries(App)

# The following are not necessary but just in case the following are added for cross-platform-ness
set_target_properties(App PROPERTIES CXX_STANDARD 17)

target_treat_all_warnings_as_errors(App)

# Options that are specific to Emscripten
if(EMSCRIPTEN)
	#Generate a full web page rather than a simple WASM module
	set_target_properties(App PROPERTIES SUFFIX ".html")

	#Enable the use of emscripten_sleep()
	target_link_options(App PRIVATE -sASYNCIFY)
endif()