/**
 * Uniforms (Step039): values that are the same for every vertex and fragment
 * of one draw call. Their memory layout MUST match `struct MyUniforms` in
 * main.cpp, field for field, because the C++ side copies that struct
 * byte-for-byte into the uniform buffer.
 */
struct MyUniforms {
	// Step043: the vec4f goes FIRST because it must start at an offset that
	// is a multiple of 16 bytes (see the layout table above `struct MyUniforms`
	// in main.cpp).
	color: vec4f, // RGBA tint applied to every fragment
	time: f32,    // Seconds since the app started
	gamma: f32,   // 2.2 if the surface is sRGB, 1.0 otherwise
	// (the 2 padding floats of the C++ struct do not need to be declared)
};

// The memory location of the uniform is given by a pair of a *bind group* and
// a *binding*. Both indices are set up on the C++ side in InitializePipeline()
// (the layout) and InitializeBindGroups() (the actual buffer).
@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;

// Step054: a constant used to build rotation angles (half a turn, in radians)
const pi = 3.14159265359;

/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput {
	@location(0) position: vec3f,
	//                        ^ This was a 2 (Step050: x, y AND z)
	@location(1) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput {
	@builtin(position) position: vec4f,
	// The location here does not refer to a vertex attribute, it just means
	// that this field must be handled by the rasterizer.
	// (It can also refer to another field of another struct that would be used
	// as input to the fragment shader.)
	@location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	//                         ^^^^^^^^^^^^ We return a custom struct
	var out: VertexOutput; // create the output struct
	let ratio = 640.0 / 480.0; // The width and height of the target surface

	// Step054: every transform is now a 4x4 MATRIX. A matrix "mixes" the
	// coordinates of a vector linearly: each output coordinate is a weighted
	// sum of the input ones, the weights being one row of the matrix.
	//
	// IMPORTANT: WGSL's mat4x4f(...) takes its 16 numbers COLUMN by column.
	// We write each matrix ROW by row (the way it looks on paper) and wrap it
	// in transpose(), which swaps rows and columns, so what you read is what
	// you get.

	// Scale the object: the diagonal multiplies x, y and z by 0.3
	let S = transpose(mat4x4f(
		0.3,  0.0, 0.0, 0.0,
		0.0,  0.3, 0.0, 0.0,
		0.0,  0.0, 0.3, 0.0,
		0.0,  0.0, 0.0, 1.0,
	));

	// Translate the object by 0.5 along x.
	// A 3x3 matrix can only MIX coordinates, it cannot ADD a constant. With
	// a 4th coordinate w that is always 1.0, the last column adds
	// "0.5 * w = 0.5" to x. These are "homogeneous coordinates".
	let T = transpose(mat4x4f(
		1.0,  0.0, 0.0, 0.5,
		0.0,  1.0, 0.0, 0.0,
		0.0,  0.0, 1.0, 0.0,
		0.0,  0.0, 0.0, 1.0,
	));

	// Rotate the model in the XY plane (around the Z axis), over time.
	// Sanity check: at angle1 = 0, c1 = 1 and s1 = 0: the identity matrix.
	let angle1 = uMyUniforms.time;
	let c1 = cos(angle1);
	let s1 = sin(angle1);
	let R1 = transpose(mat4x4f(
		 c1,  s1, 0.0, 0.0,
		-s1,  c1, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0,
	));

	// Tilt the view point in the YZ plane (around the X axis) by three 8th
	// of a turn (1 turn = 2 pi), so we look at the pyramid from the side.
	// This one does not change over time.
	let angle2 = 3.0 * pi / 4.0;
	let c2 = cos(angle2);
	let s2 = sin(angle2);
	let R2 = transpose(mat4x4f(
		1.0, 0.0, 0.0, 0.0,
		0.0,  c2,  s2, 0.0,
		0.0, -s2,  c2, 0.0,
		0.0, 0.0, 0.0, 1.0,
	));

	// Compose and apply. A product of matrices reads RIGHT TO LEFT:
	// first S (scale), then T (translate), then R1 (spin), then R2 (tilt).
	// The order matters: T * S moves the scaled object by 0.5, but S * T
	// would also scale the translation (0.5 * 0.3 = 0.15).
	let homogeneous_position = vec4f(in.position, 1.0);
	let position = (R2 * R1 * T * S * homogeneous_position).xyz;

	// Step052: we now output the real depth instead of 0.0, so the depth test
	// can tell which triangle is in front.
	// WebGPU keeps only the depths between 0.0 (near) and 1.0 (far). Our
	// pyramid's z goes from about -1 to +1, so we squeeze it into 0..1 with
	// z * 0.5 + 0.5. This is a temporary trick: the projection matrix of the
	// next chapters does this properly.
	out.position = vec4f(position.x, position.y * ratio, position.z * 0.5 + 0.5, 1.0);
	out.color = in.color; // forward the color attribute to the fragment shader
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	//     ^^^^^^^^^^^^^^^^ Use for instance the same struct as what the vertex outputs
	// Gamma-correction [CODE THAT WAS MISSING IN Step037_cpp]
	// The value used to be computed and then thrown away (we returned
	// in.color). We now return it, with an exponent chosen on the C++ side so
	// the colors look the same whatever surface format the backend picked.
	// Step043: tint the vertex color with the uniform color. Multiplying two
	// colors channel by channel acts like looking through a colored filter.
	let color = in.color * uMyUniforms.color.rgb;
	let linear_color = pow(color, vec3f(uMyUniforms.gamma));
	// Step044: the alpha comes from the uniform color, so a color with an
	// alpha below 1.0 is see-through thanks to the pipeline's blend state.
	return vec4f(linear_color, uMyUniforms.color.a); // use the interpolated color coming from the vertex shader
}
