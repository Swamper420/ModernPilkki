#version 450
// Iteration 6 scaffold: layout-A static mesh PS.
// Clean-room stand-in for mesh PS 00750138 (docs/render.md 3): one sampled
// texture modulated by the vertex shade.
layout(location = 0) in vec2 uv;
layout(location = 1) in float shade;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 o;

void main() {
  vec4 c = texture(tex, uv);
  // Clean-room colour-key: the .pcx textures are 8-bit paletted with no alpha
  // and use magenta (255,0,255) as the transparent colour; cut it out like the
  // original's alpha test. The terrain/water texture never contains magenta.
  if (c.r > 0.9 && c.g < 0.15 && c.b > 0.9) discard;
  o = vec4(c.rgb * shade, c.a);
}
