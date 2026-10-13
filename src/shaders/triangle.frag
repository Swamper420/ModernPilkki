#version 450
// Iteration 4 scaffold: flat colour, no texture/sampler.
// Clean-room stand-in for mesh PS 00750138 (docs/render.md 3).
layout(location = 0) out vec4 o;
void main() { o = vec4(0.80, 0.90, 1.00, 1.00); }
