
Step_043_cpp

# More uniforms

## Introduction

In the previous chapter our uniform was a single `f32` called `uTime`. In practice we always end up wanting more than one value: a color, a time, a camera matrix... Instead of creating one uniform per value we put all of them in a **struct**, exactly like we did for the vertex shader input.

## Shader side

The uniform becomes a struct. We add a `color` field and rename the variable `uMyUniforms`:

```rust
struct MyUniforms {
    color: vec4f,
    time: f32,
    gamma: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
```

The vertex shader reads `uMyUniforms.time` and the fragment shader tints the color:

```rust
let color = in.color * uMyUniforms.color.rgb;
let linear_color = pow(color, vec3f(uMyUniforms.gamma));
```

>[!note]
>`gamma` is NOT in the guide. We added it in Step039 because the right gamma depends on the surface format (2.2 for sRGB surfaces like wgpu-native, 1.0 for Dawn and Chrome). The guide always uses 2.2.

## Buffer

On the C++ side we mirror the struct:

```Cpp
struct MyUniforms {
    std::array<float, 4> color;
    float time;
    float gamma;
    float _pad[2];
};
static_assert(sizeof(MyUniforms) % 16 == 0);
```

>[!note]
>Don't forget `#include <array>` for `std::array`.

The buffer size is now simply `sizeof(MyUniforms)` (32 bytes) instead of `4 * sizeof(float)`. The bind group layout's `minBindingSize` and the bind group entry's `size` use `sizeof(MyUniforms)` too, and the binding is visible to **both** the vertex and the fragment stage.

To update only one field each frame we use `offsetof`, so the code keeps working even if we reorder the struct:

```Cpp
float t = static_cast<float>(glfwGetTime());
queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, time), &t, sizeof(float));
```

## Memory Layout Constraints

This is the important part of the chapter. The GPU reads the buffer with **strict rules** on where each field can be. If the C++ struct and the WGSL struct don't agree byte for byte, the shader silently reads garbage.

### Alignment

Every type has an **alignment**: its offset in the struct must be a multiple of that number.

| Type | Size | Alignment |
|---|---|---|
| `f32` | 4 bytes | 4 |
| `vec2f` | 8 bytes | 8 |
| `vec3f` | 12 bytes | **16** |
| `vec4f` | 16 bytes | 16 |

If we wrote `time` first, `color` would start at offset 4. 4 is not a multiple of 16, so it is **invalid**. The fix is to put the big field first:

```
offset  0: color  (16 bytes)
offset 16: time   ( 4 bytes)
offset 20: gamma  ( 4 bytes)
offset 24: _pad   ( 8 bytes)  -> total 32
```

>[!IMPORTANT]
>`vec3f` is a trap: it is 12 bytes but aligned like a `vec4f` (16). We will meet it again with normals and light directions.

### Padding

The whole struct must have a size that is a multiple of its **largest alignment** (16 here). 24 bytes is not, so we pad to 32 with `_pad[2]`. The WGSL struct does NOT declare the padding, WGSL adds it on its own.

We added a second check that the guide does not have:

```Cpp
static_assert(offsetof(MyUniforms, time) == 16, "time must start right after the 16-byte color");
```

If someone reorders the struct by mistake, the program does not compile anymore instead of drawing garbage.

>[!note]
>When several bindings share ONE buffer, each binding's offset must be a multiple of `minUniformBufferOffsetAlignment` (often 256 bytes). We will need this in "Dynamic uniforms".

## Conclusion

A uniform struct lets us send many values at once. The price is that we must respect the alignment and padding rules on the C++ side by hand. The guide's author made a tool for this: WebGPU-AutoLayout generates the C++ struct from the WGSL struct.

Resulting code: branch `step043` of LearnWebGPU-Code.
