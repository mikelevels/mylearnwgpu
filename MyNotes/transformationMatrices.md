
Step_054_cpp

# Transformation matrices

>[!note]
>This chapter only changes the SHADER, so there was nothing to translate from the older WebGPU version. The C++ side did not move.

## Why matrices?

So far we moved things with ad-hoc formulas: an offset here, a hand-written rotation there, a `ratio` somewhere else. A **matrix** is ONE tool for all of them.

A matrix **linearly mixes** the coordinates of a vector: each output coordinate is a weighted sum of the input coordinates, the weights being one row of the matrix.

>[!note]
>"Linearly" means we can only scale and add the coordinates. Never multiply them together (no `x * y`).

`M * a` means "transform the position `a` with the matrix `M`".

## Scale

A scale is a matrix with the factors on its diagonal and 0 everywhere else:

```rust
let S = transpose(mat4x4f(
	0.3,  0.0, 0.0, 0.0,
	0.0,  0.3, 0.0, 0.0,
	0.0,  0.0, 0.3, 0.0,
	0.0,  0.0, 0.0, 1.0,
));
```

## Translation and the 4th coordinate

A 3x3 matrix can MIX x, y, z but it cannot ADD a constant (+0.5 to x). Trick: we add a 4th coordinate **w that is always 1.0**. Then the last column of a 4x4 matrix adds "column value x 1.0" to each coordinate:

```rust
let T = transpose(mat4x4f(
	1.0,  0.0, 0.0, 0.5,// x' = x + 0.5 * w = x + 0.5
	0.0,  1.0, 0.0, 0.0,
	0.0,  0.0, 1.0, 0.0,
	0.0,  0.0, 0.0, 1.0,
));
let homogeneous_position = vec4f(in.position, 1.0);
```

These are called **homogeneous coordinates**. The upper-left 3x3 block holds scale and rotation, the 4th column holds the translation.

>[!note]
>Linear transform + translation = **affine** transform.

## Rotation

A rotation mixes two axes with cos and sin of the angle. Around Z (in the XY plane):

```rust
let R1 = transpose(mat4x4f(
	 c1,  s1, 0.0, 0.0,
	-s1,  c1, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0,
));
```

>[!note]
>Rotations always turn around the ORIGIN. To turn around another point: translate it to the origin, rotate, translate back.

## The transpose() trick

>[!IMPORTANT]
>WGSL's `mat4x4f(...)` takes its 16 numbers **column by column**. On paper we read matrices **row by row**. Wrapping the constructor in `transpose()` (swap rows and columns) lets us write the matrix as it looks on paper. For a diagonal matrix like S it changes nothing, for T and R it matters a lot.

## Order of composition

A product of matrices reads **right to left**:

```rust
let position = (R2 * R1 * T * S * homogeneous_position).xyz;
// S first (scale), then T (translate), then R1 (spin), then R2 (tilt the view)
```

>[!IMPORTANT]
>The order matters! `T * S` moves the small pyramid by 0.5. `S * T` also scales the translation: it moves by 0.5 * 0.3 = 0.15. Matrix products are associative (we can group them as we like) but NOT commutative (we cannot swap them).

`R2` tilts our point of view by 3/8 of a turn (`3.0 * pi / 4.0`). Moving the object or moving the viewpoint are almost the same thing, but the difference matters as soon as there are several objects or lights.

>[!note]
>The guide says WGSL only has square matrices, so `mat4x3f` "cannot be used". That is NOT right: WGSL has every size from `mat2x2f` to `mat4x4f`, including non-square ones like `mat4x3f`. The real reason to use 4x4 is that only square matrices can be chained with `*` over and over.

## Build trap fixed in this step

This chapter only changes `shader.wgsl`, which is exactly the case where the emscripten build used to say "no work to do" and keep the OLD shader. `CMakeLists.txt` now sets `LINK_DEPENDS` on every file in `resources/`, so editing only a resource re-packs `App.data`. (Tested: touching only `shader.wgsl` now triggers "Linking CXX executable App.html".)

## Conclusion

The pyramid is small, off-center, spinning, and seen from the side. It still has no PERSPECTIVE: things far away are not smaller. That is "Projection matrices", the next chapter.

Resulting code: branch `step054` of LearnWebGPU-Code.
