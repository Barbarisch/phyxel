#version 450

// Blit the graded image to the swapchain.
//
// This exists so there is exactly ONE composited image. post_process.frag now renders into an
// offscreen "grade" target rather than straight to the swapchain, and this pass copies that target
// to the screen. The editor viewport samples the SAME grade image, so what the editor shows and what
// a packaged game presents are the same pixels.
//
// That is the whole point: bloom, SSAO and the tonemap were all disabled for years because the
// editor sampled the RAW scene image and nobody could see the composite. A defect in post-process
// could ship unseen. It cannot now.
//
// Deliberately a straight copy — no colour maths here. Grading belongs in post_process.frag, where
// both the swapchain and the editor see it.

layout(set = 0, binding = 0) uniform sampler2D gradedImage;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(gradedImage, inUV);
}
