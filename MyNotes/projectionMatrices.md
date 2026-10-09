
Step_055_cpp

# Projection matrices

>[!IMPORTANT]
>Marked as written for an OLDER version of WebGPU, but the WebGPU calls in this chapter are just `writeBuffer` and limits, which are the same in our version. The new things are math and a library: **GLM**.

## Why the pyramid looked flat

Up to Step054 we drew with an **orthographic** look: x and y went straight to the screen, z was only used for depth. Far things were not smaller. Real eyes and cameras see in **perspective**: the farther something is, the smaller it looks.

## The perspective division

The trick is in the 4th coordinate `w`. After the vertex shader, the GPU **divides x, y and z by w**. Until now w was always 1.0, so nothing happened. A perspective matrix puts the DEPTH into w:

```rust
fn makePerspectiveProj(ratio: f32, near: f32, far: f32, focalLength: f32) -> mat4x4f {
	let divides = 1.0 / (far - near);
	return transpose(mat4x4f(
		focalLength,         0.0,              0.0,               0.0,
		    0.0,     focalLength * ratio,      0.0,               0.0,
		    0.0,             0.0,         far * divides, -far * near * divides,
		    0.0,             0.0,              1.0,               0.0,// w = z
	));
}
```

- Last row `(0, 0, 1, 0)`: w = z. Dividing by w = dividing by the distance. Far = small.
- Third row: maps z from `[near, far]` to the depth range `[0, 1]`. It REPLACES the `z * 0.5 + 0.5` trick of Step052.
- `focalLength`: bigger = zoomed in (narrower field of view).

>[!note]
>`near` must NOT be 0: things at distance 0 would be divided by 0. And keep `far / near` reasonable, the depth buffer only has 24 bits to share between near and far.

## Model, view, projection

We now split the transform in three matrices, applied right to left:

```rust
out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
```

| Matrix | Answers | Here |
|---|---|---|
| model | Where is the object in the world? | scale 0.3, move 0.5 along x, spin with time |
| view | Where is the camera and where does it look? | tilted 3/8 of a turn, moved back to z = -2 |
| projection | How is 3D flattened onto the screen? | perspective, focal length 2 |

>[!note]
>Moving the camera back by 2 is the same as moving the whole world forward by 2. That is why the view translation uses `-focalPoint`.

## Computing the matrices on the CPU

In Step054 the shader rebuilt the same matrices for EVERY vertex. Now they are computed once on the CPU (C++) and sent in the uniform buffer. The shader keeps the old way as `vs_main_optionA` (not used) for comparison.

The uniform struct grew, matrices first because they are aligned to 16 bytes:

```
offset   0: projectionMatrix (64 bytes)
offset  64: viewMatrix       (64 bytes)
offset 128: modelMatrix      (64 bytes)
offset 192: color            (16 bytes)
offset 208: time, 212: gamma, 216: padding -> 224 bytes
```

Each frame only the model matrix changes (the spin), so only those 64 bytes are re-uploaded with `offsetof(MyUniforms, modelMatrix)`.

>[!IMPORTANT]
>`maxUniformBufferBindingSize` was 64 bytes (16 floats). The struct is now 224 bytes. wgpu-native applies the limits we ask for EXACTLY and would refuse the bind group. Dawn quietly raises small limits to its defaults, so Dawn alone would NOT have shown the bug. We now ask for 256 bytes. (Found by the design review before it bit us.)

## GLM

GLM ("OpenGL Mathematics") gives C++ the same `vec3`, `vec4`, `mat4x4` as the shaders. It is **header-only**: the whole library is the `glm/` folder (GLM 0.9.9.8, MIT license), copied from the guide's own repository. `CMakeLists.txt` adds the project root to the include path, and turns off MSVC's warning C4201 (GLM uses "nameless structs").

Two defines MUST come before `#include <glm/glm.hpp>`:

```Cpp
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // WebGPU depth is 0..1 (OpenGL's is -1..1)
#define GLM_FORCE_LEFT_HANDED       // z goes INTO the screen, like WebGPU
```

The guide shows three ways to build the same matrices, and `InitializeBuffers()` keeps all three (each overwrites the previous one):

- **Option A:** by hand, the same numbers as Step054's shader.
- **Option B:** one GLM call per matrix: `glm::scale`, `glm::translate`, `glm::rotate`, then multiply.
- **Option C (used):** chain the calls on one matrix.

>[!IMPORTANT]
>Option C's trap: `M = glm::rotate(M, ...)` multiplies on the RIGHT. So the calls are written in the OPPOSITE order of what happens to the vertex: rotate is written first but applied LAST.

`glm::perspective` takes a vertical **field of view** instead of a focal length: `fov = 2 * atan(1 / focalLength)`.

## CMakeLists.txt question answered

Your old comment asked whether the `if (MSVC) ... /W4` block is where warnings become errors. It is NOT: `COMPILE_WARNING_AS_ERROR ON` (just above it) does that. The `/W4` and `-Wall -Wextra -pedantic` block chooses how MANY warnings are reported.

## Conclusion

The pyramid now orbits the center of the screen and gets bigger and smaller as it comes closer and goes away: real perspective. Next: "Basic shading", so the faces get light and shadow instead of flat colors.

Resulting code: branch `step055` of LearnWebGPU-Code.
