# BUG FIX (bug review): was 3.0. We use COMPILE_WARNING_AS_ERROR, which only
# exists since CMake 3.24: an older CMake would silently ignore it.
cmake_minimum_required(VERSION 3.24...3.31)
project(
	LearnWebGPU
	VERSION 0.1.0
	LANGUAGES CXX C
)

if (NOT EMSCRIPTEN)
	add_subdirectory(glfw)
else()
	add_library(glfw INTERFACE)
	target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_subdirectory(webgpu)
add_subdirectory(glfw3webgpu)

add_executable(App
	main.cpp
	# Add the ResourceManager files
	ResourceManager.h
	ResourceManager.cpp
)

target_link_libraries(App PRIVATE glfw webgpu glfw3webgpu)

# Step055: GLM is a header-only library copied into the glm/ folder at the
# root of the project. Adding the project root to the include path is what
# makes `#include <glm/glm.hpp>` work.
target_include_directories(App PRIVATE .)

# We add an option to enable different settings when developing the app than
# when distributing it.
option(DEV_MODE "Set up development helper settings" ON)

if(EMSCRIPTEN)
    # In the browser, the resources folder is packed into App.data and mounted
    # at /resources in the virtual file system (see --preload-file below), in
    # BOTH modes. BUG FIX (bug review): before, the folder kept your absolute
    # Windows path inside the browser, so DEV_MODE=OFF ("./resources") could
    # not find the files, and your disk path was baked into the web page.
    target_compile_definitions(App PRIVATE
        RESOURCE_DIR="/resources"
    )
elseif(DEV_MODE)
    # In dev mode, we load resources from the source tree, so that when we
    # dynamically edit resources (like shaders), these are correctly
    # versionned.
    target_compile_definitions(App PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
else()
    # In release mode, we just load resources relatively to wherever the
    # executable is launched from, so that the binary is portable
    target_compile_definitions(App PRIVATE
        RESOURCE_DIR="./resources"
    )
endif()

target_copy_webgpu_binaries(App)

set_target_properties(App PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	COMPILE_WARNING_AS_ERROR ON
)

# Answer to the old question that was here ("is this where warnings become
# errors?"): no. COMPILE_WARNING_AS_ERROR ON just above is what turns every
# warning into an error. The block below chooses HOW MANY warnings the
# compiler reports: /W4 (MSVC) and -Wall -Wextra -pedantic (clang/emscripten)
# are both "a lot".
if (MSVC)
	target_compile_options(App PRIVATE /W4)
	# Step055: GLM uses "nameless structs/unions", a common compiler extension
	# that MSVC warns about at /W4 (C4201). Since warnings are errors, we turn
	# this one off, like the guide does.
	target_compile_options(App PRIVATE /wd4201)
else()
	target_compile_options(App PRIVATE -Wall -Wextra -pedantic)
endif()

if (XCODE)
	set_target_properties(App PROPERTIES
		XCODE_GENERATE_SCHEME ON
		XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
	)
endif()

if (EMSCRIPTEN)
	set_target_properties(App PROPERTIES SUFFIX ".html")
	target_link_options(App PRIVATE
		-sASYNCIFY
		# "@/resources": where the folder appears in the browser (RESOURCE_DIR above)
		--preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
	)

	# The browser cannot read files from your disk, so the resources folder is
	# packed into App.data when the app is LINKED (--preload-file above).
	# But the build system only re-links when a .cpp/.h file changes: editing
	# only shader.wgsl used to say "no work to do" and the browser kept the
	# OLD shader. LINK_DEPENDS tells the build that the link also depends on
	# every file in resources/. CONFIGURE_DEPENDS re-scans the folder when a
	# file is added or removed.
	file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
	set_target_properties(App PROPERTIES LINK_DEPENDS "${RESOURCE_FILES}")
endif()