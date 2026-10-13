#version 450
// Iteration 5 scaffold: layout-B textured quad PS.
// Clean-room stand-in for the 2D/terrain/textured-2D PS 007513c0 / 00751e98
// (docs/render.md 3): one sampled texture, alpha from the vertex.
layout(location = 0) in vec2 uv;
layout(location = 1) in float alpha;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 o;

void main() { o = texture(tex, uv) * vec4(1.0, 1.0, 1.0, alpha); }
