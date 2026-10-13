#version 450
// Iteration 4 scaffold: hardcoded triangle, positions from gl_VertexIndex.
// Clean-room stand-in for the original static-mesh VS 0074c370 (docs/render.md
// 3): same stage contract (world -> clip, D3D clip space z 0..1), no vertex
// buffer bound yet. Replaced by the real mesh/terrain VS in a later iteration.
void main() {
  vec2 p[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
  gl_Position = vec4(p[gl_VertexIndex], 0.5, 1.0);
}
