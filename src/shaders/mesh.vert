#version 450
// Iteration 6 scaffold: layout-A static mesh VS.
// Clean-room stand-in for the static-mesh VS 0074c370 (docs/render.md 3):
// consumes layout A (pos3, normal3, uv2). Positions arrive centered + unit
// extent (render3d_mesh.hpp); a rotation uniform stands in for the model
// matrix (00599d40) and the camera (0064f470) still under decompile.
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv;

layout(set = 1, binding = 0) uniform U { mat4 model; } u;

layout(location = 0) out vec2 o_uv;
layout(location = 1) out float o_shade;

void main() {
  gl_Position = u.model * vec4(pos, 1.0);
  o_uv = uv;
  // Flat directional shade; |dot| so back faces don't go black (same idea as
  // the CPU path in models.hpp FaceShade).
  o_shade = 0.45 + 0.55 * abs(dot(normalize(nrm), normalize(vec3(-0.4, 0.6, 0.7))));
}
