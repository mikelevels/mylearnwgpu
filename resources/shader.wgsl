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

/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput {
	@location(0) position: vec2f,
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
	// The offset that we want to apply to the position. It is now a `var`
	// (not `let`) because we modify it: the logo moves in a circle of radius
	// 0.3 as time goes on.
	var offset = vec2f(-0.6875, -0.463);
	offset += 0.3 * vec2f(cos(uMyUniforms.time), sin(uMyUniforms.time));
	out.position = vec4f(in.position.x + offset.x, (in.position.y + offset.y) * ratio, 0.0, 1.0);
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
	return vec4f(linear_color, 1.0); // use the interpolated color coming from the vertex shader
}
