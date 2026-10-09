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

	// Step050: the pyramid is centered on the origin, so no more offset.
	// Instead it ROTATES around the X axis. The angle is the time in seconds,
	// read as radians: one full turn every 2*pi = ~6.3 seconds.
	let angle = uMyUniforms.time; // you can multiply it to rotate faster

	// Rotating around X keeps x as is and "mixes" y and z:
	//   y' = cos(angle) * y + sin(angle) * z
	//   z' = cos(angle) * z - sin(angle) * y
	// Sanity check: at angle = 0, alpha = 1 and beta = 0, so nothing moves.
	// (The minus sign is there because swapping two axes would mirror the
	// object; after a quarter turn z' must be -y, not +y.)
	let alpha = cos(angle);
	let beta = sin(angle);
	var position = vec3f(
		in.position.x,
		alpha * in.position.y + beta * in.position.z,
		alpha * in.position.z - beta * in.position.y,
	);

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
