
Step_050_cpp

# A simple example (3D Meshes)

>[!IMPORTANT]
>This chapter is marked as written for an OLDER version of WebGPU. The guide's `step050` code still uses a `SwapChain` (we use `surface.configure()`), `requiredFeaturesCount` (we have `requiredFeatureCount`) and puts everything in `main`. Only the 3D ideas below were brought over.

## From 2D to 3D

Our points get a third coordinate, **z**. We use a new file, `resources/pyramid.txt`: a square base at z = -0.3 and a tip at z = +0.5.

```
[points]
# x   y   z      r   g   b
-0.5 -0.5 -0.3    1.0 1.0 1.0
...
+0.0 +0.0 +0.5    0.5 0.5 0.5

[indices]
 0  1  2
 ...
```

>[!note]
>The tip is grey on purpose (0.5 0.5 0.5) so we can tell it apart from the base. Real lighting ("Basic shading") comes later.

## Loading

`loadGeometry` takes a new `dimensions` argument (2 for `webgpu.txt`, 3 for `pyramid.txt`). Each point line has `dimensions + 3` numbers: the position then the color.

```Cpp
ResourceManager::loadGeometry(RESOURCE_DIR "/pyramid.txt", pointData, indexData, 3 /* dimensions */);
```

>[!IMPORTANT]
>The guide's `pyramid.txt` ends with a line that contains a single SPACE. Our stricter loader from Step039 refused it ("Could not load geometry!"), because a line with a space is not `empty()`. The guide's own loader did not complain: it silently added a fake triangle (4, 4, 4) made of the last index it had read. Our loader now treats lines that are only spaces/tabs as blank (`line.find_first_not_of(" \t")`).
>
>Lesson: a loader that refuses bad input finds bugs in the DATA. A loader that guesses hides them.

## Vertex layout

The position is now 3 floats, so everything after it moves:

```Cpp
vertexAttribs[0].format = wgpu::VertexFormat::Float32x3;// was Float32x2
vertexAttribs[1].offset = 3*sizeof(float);// was 2
vertexBufferLayout.arrayStride = 6*sizeof(float);// was 5
```

And the device limit must follow: `maxVertexBufferArrayStride = 6*sizeof(float)`.

In the shader: `@location(0) position: vec3f` (was `vec2f`).

## Rotating by hand

No matrices yet. To see the 3D shape we rotate it around the **X axis** in the vertex shader, using the time as the angle:

```rust
let angle = uMyUniforms.time;// in RADIANS
let alpha = cos(angle);
let beta = sin(angle);
var position = vec3f(
	in.position.x,
	alpha * in.position.y + beta * in.position.z,
	alpha * in.position.z - beta * in.position.y,
);
out.position = vec4f(position.x, position.y * ratio, 0.0, 1.0);
```

>[!note]
>Can't remember which one is cos and which one is sin? Check angle = 0: alpha = 1 and beta = 0, so the position must not change. Only `alpha = cos` works.

>[!note]
>Angles are in radians: degrees * pi / 180. One full turn is 2 * pi, about 6.3 seconds here.

## Rolled back

The guide rolls back the dynamic uniforms of Step044 (one logo, one uniform block again) and sets the offset to 0. We did the same. The Step044 code is still in git: commit `f446f3e`.

## The problem we end on

The pyramid looks WRONG: some faces that are behind are drawn on top of faces in front. We still write `0.0` as the output z, so the GPU has no idea what is in front. It just paints triangles in the order they come in the index buffer, and the LAST one wins.

This is what the next chapter fixes with a **depth buffer**.

Resulting code: branch `step050` of LearnWebGPU-Code.
