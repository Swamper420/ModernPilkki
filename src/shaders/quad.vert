#version 450
// Iteration 5 scaffold: layout-B textured quad VS.
// Clean-room stand-in for the 2D/terrain VS pair 0074dce8 / 0074eec0
// (docs/render.md 3): consumes the same layout-B vertex (pos2, uv2, depth,
// alpha), maps pixel space -> clip. The viewport uniform stands in for the
// original viewport projection in CB0 (layout still open, docs/render.md 13).
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in float depth;
layout(location = 3) in float alpha;

layout(set = 1, binding = 0) uniform U { vec2 viewport; } u;

layout(location = 0) out vec2 o_uv;
layout(location = 1) out float o_alpha;

void main() {
  vec2 ndc = pos / u.viewport * 2.0 - 1.0;
  gl_Position = vec4(ndc, depth, 1.0);
  o_uv = uv;
  o_alpha = alpha;
}
