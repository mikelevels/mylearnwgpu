/**
 * Uniforms (Step039): values that are the same for every vertex and fragment
 * of one draw call. Their memory layout MUST match `struct MyUniforms` in
 * main.cpp, field for field, because the C++ side copies that struct
 * byte-for-byte into the uniform buffer.
 */
struct MyUniforms {
	// Step055: the transform matrices are now computed ONCE per frame on the
	// CPU (main.cpp) instead of once per vertex here. 64 bytes each, aligned
	// to 16, so they go first (see the layout table above `struct MyUniforms`
	// in main.cpp).
	projectionMatrix: mat4x4f, // camera space -> clip space (perspective)
	viewMatrix: mat4x4f,       // world space -> camera space
	modelMatrix: mat4x4f,      // model space -> world space
	// Step043: the vec4f must start at an offset that is a multiple of 16
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

/**
 * Step055: an ORTHOGRAPHIC projection: no perspective, far things keep their
 * size. It only scales x and y and maps z from [near, far] to [0, 1].
 * (Not used, kept to compare with the perspective one.)
 */
fn makeOrthographicProj(ratio: f32, near: f32, far: f32, scale: f32) -> mat4x4f {
	return transpose(mat4x4f(
		1.0 / scale,      0.0,           0.0,                  0.0,
		    0.0,     ratio / scale,      0.0,                  0.0,
		    0.0,          0.0,      1.0 / (far - near), -near / (far - near),
		    0.0,          0.0,           0.0,                  1.0,
	));
}

/**
 * Step055: a PERSPECTIVE projection. The last row (0, 0, 1, 0) copies z into
 * the 4th coordinate w. After the vertex shader, the GPU divides x, y and z by
 * w ("perspective division"), so the farther a point is (big z), the closer to
 * the center of the screen it ends up: far things look smaller.
 */
fn makePerspectiveProj(ratio: f32, near: f32, far: f32, focalLength: f32) -> mat4x4f {
	let divides = 1.0 / (far - near);
	return transpose(mat4x4f(
		focalLength,         0.0,              0.0,               0.0,
		    0.0,     focalLength * ratio,      0.0,               0.0,
		    0.0,             0.0,         far * divides, -far * near * divides,
		    0.0,             0.0,              1.0,               0.0,
	));
}

/**
 * Option A: rebuild all the matrices for EACH vertex, like in Step054, plus a
 * view translation and a projection.
 * (Not recommended, and not used: every vertex recomputes the exact same
 * matrices. Kept because it shows all the math in one place.)
 */
fn vs_main_optionA(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	let ratio = 640.0 / 480.0;

	// Scale the object
	let S = transpose(mat4x4f(
		0.3,  0.0, 0.0, 0.0,
		0.0,  0.3, 0.0, 0.0,
		0.0,  0.0, 0.3, 0.0,
		0.0,  0.0, 0.0, 1.0,
	));

	// Translate the object
	let T = transpose(mat4x4f(
		1.0,  0.0, 0.0, 0.5,
		0.0,  1.0, 0.0, 0.0,
		0.0,  0.0, 1.0, 0.0,
		0.0,  0.0, 0.0, 1.0,
	));

	// Rotate the model in the XY plane
	let angle1 = uMyUniforms.time;
	let c1 = cos(angle1);
	let s1 = sin(angle1);
	let R1 = transpose(mat4x4f(
		 c1,  s1, 0.0, 0.0,
		-s1,  c1, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0,
	));

	// Tilt the view point in the YZ plane by three 8th of turn
	let angle2 = 3.0 * pi / 4.0;
	let c2 = cos(angle2);
	let s2 = sin(angle2);
	let R2 = transpose(mat4x4f(
		1.0, 0.0, 0.0, 0.0,
		0.0,  c2,  s2, 0.0,
		0.0, -s2,  c2, 0.0,
		0.0, 0.0, 0.0, 1.0,
	));

	// Move the view point back, so the object is in front of the camera
	let focalPoint = vec3f(0.0, 0.0, -2.0);
	let T2 = transpose(mat4x4f(
		1.0,  0.0, 0.0, -focalPoint.x,
		0.0,  1.0, 0.0, -focalPoint.y,
		0.0,  0.0, 1.0, -focalPoint.z,
		0.0,  0.0, 0.0,     1.0,
	));

	// Compose and apply (S then T then R1 then R2 then T2, reads backwards)
	let homogeneous_position = vec4f(in.position, 1.0);
	let viewspace_position = T2 * R2 * R1 * T * S * homogeneous_position;

	// Orthographic projection
	//let P = makeOrthographicProj(ratio, -1.0 /* near */, 1.0 /* far */, 1.0 /* scale */);

	// Perspective projection
	let P = makePerspectiveProj(ratio, 0.01 /* near */, 100.0 /* far */, 2.0 /* focalLength */);

	// No more ".xyz" and no more "z * 0.5 + 0.5" trick: the projection keeps
	// w (which is NOT 1.0 anymore) and maps the depth into [0, 1] itself.
	out.position = P * viewspace_position;

	out.color = in.color;
	return out;
}

/**
 * Option B: use the matrices precomputed on the CPU and stored in the
 * uniform buffer. (Recommended, and used.)
 * Same right-to-left reading: model first, then view, then projection.
 */
fn vs_main_optionB(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
	out.color = in.color; // forward the color attribute to the fragment shader
	return out;
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	//return vs_main_optionA(in);
	return vs_main_optionB(in);
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
