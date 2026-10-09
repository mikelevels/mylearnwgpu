
Step_044_cpp

# Dynamic uniforms

>[!IMPORTANT]
>This chapter is marked as written for an OLDER version of WebGPU (and with no `Application` class, everything is in `main`). The guide's code does NOT paste into our project as is. For example it uses `requiredFeaturesCount` (we have `requiredFeatureCount`) and `timestampWriteCount` (gone in our version). The ideas are the same, the code below is the version that works with OUR backends.

## Motivation

We want to draw the logo **twice** in the same frame with **different uniforms** (different color, different time).

The first idea is to call `writeBuffer` between the two draw calls:

```Cpp
// THIS WON'T WORK
queue.writeBuffer(uniformBuffer, 0, &uniforms1, sizeof(MyUniforms));
renderPass.drawIndexed(indexCount, 1, 0, 0, 0);
queue.writeBuffer(uniformBuffer, 0, &uniforms2, sizeof(MyUniforms));
renderPass.drawIndexed(indexCount, 1, 0, 0, 0);
```

It does not work because the render pass only **records** the draw calls into a command buffer. Nothing is drawn until `queue.submit()`. All the `writeBuffer` calls are executed **before** the submitted commands, so both draws read the LAST value we wrote.

## The solution: one buffer, two blocks, a dynamic offset

We put both uniform blocks in the SAME buffer, one after the other, and tell each draw call **where to start reading**. That start position is the **dynamic offset**.

```
uniformBuffer:
offset 0              block 0 (32 bytes) -> first logo
offset 32..255        unused padding
offset uniformStride  block 1 (32 bytes) -> second logo
```

### Offset alignment

A dynamic offset cannot be anything we want: it must be a multiple of the device limit `minUniformBufferOffsetAlignment` (256 bytes on this machine, for all three backends). So the distance between the blocks, the **stride**, is `sizeof(MyUniforms)` rounded UP to that alignment:

```Cpp
uint32_t ceilToNextMultiple(uint32_t value, uint32_t step) {
	uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
	return step * divide_and_ceil;
}

wgpu::SupportedLimits deviceSupportedLimits;
device.getLimits(&deviceSupportedLimits);
uniformStride = ceilToNextMultiple(
	static_cast<uint32_t>(sizeof(MyUniforms)),
	deviceSupportedLimits.limits.minUniformBufferOffsetAlignment
);
bufferDesc.size = uniformStride + sizeof(MyUniforms);// 256 + 32 = 288 bytes
```

>[!note]
>We read the limit from the **device** (after creation), not the adapter. In `GetRequiredLimits()` we already asked for the adapter's best (smallest) alignment, so the two match.

### Writing the two blocks

```Cpp
queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));// block 0
uniforms.time = -1.0f;
uniforms.color = { 1.0f, 1.0f, 1.0f, 0.7f };
queue.writeBuffer(uniformBuffer, uniformStride, &uniforms, sizeof(MyUniforms));// block 1
```

>[!note]
>Don't forget the NON-ZERO offset in the second `writeBuffer`, or you overwrite block 0.

Each frame only block 0's `time` is updated, so the first logo moves and the second one stays still.

## Binding layout

One line: the binding now accepts a dynamic offset.

```Cpp
bindingLayout.buffer.hasDynamicOffset = true;
```

The bind group entry does not change: `offset = 0` and `size = sizeof(MyUniforms)`. The dynamic offset is **added** to that base offset at draw time.

## Draw calls

The last two arguments of `setBindGroup` (that we used to leave at `0, nullptr`) are the number of dynamic offsets and a pointer to them:

```Cpp
uint32_t dynamicOffset = 0;

dynamicOffset = 0 * uniformStride;
renderPass.setBindGroup(0, bindGroup, 1, &dynamicOffset);
renderPass.drawIndexed(indexCount, 1, 0, 0, 0);

dynamicOffset = 1 * uniformStride;
renderPass.setBindGroup(0, bindGroup, 1, &dynamicOffset);
renderPass.drawIndexed(indexCount, 1, 0, 0, 0);
```

## Device limits

```Cpp
requiredLimits.limits.maxDynamicUniformBuffersPerPipelineLayout = 1;
```

>[!IMPORTANT]
>The guide keeps `maxBufferSize = 15 * 5 * sizeof(float)` (300 bytes). Our uniform buffer is 288 bytes, so it only fits **by luck** because the alignment is 256. On a GPU with a 512 alignment it would fail. We now compute `maxBufferSize` as the biggest of our two buffers.

## Shader

The alpha now comes from the uniform color, so the second logo (alpha 0.7) is see-through. This works because of the blend state we set up in "Hello Triangle" (`SrcAlpha` / `OneMinusSrcAlpha`).

```rust
return vec4f(linear_color, uMyUniforms.color.a);
```

## Conclusion

Dynamic offsets let one bind group serve many draw calls with different data. This is how we will later draw many objects with one pipeline, each with its own model matrix.

Resulting code: branch `step044` of LearnWebGPU-Code.
