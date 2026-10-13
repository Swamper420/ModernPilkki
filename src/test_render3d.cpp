// Smallest assert: letterbox math (aspect preserved/centered/degenerate), the
// clear-grey factor, and the iteration-2 4x4 matrix math (render3d_math.hpp).
// No SDL window or GPU device needed.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "obj.hpp"
#include "render3d.hpp"
#include "render3d_camera.hpp"
#include "render3d_layout.hpp"
#include "render3d_math.hpp"
#include "render3d_mesh.hpp"
#include "render3d_scene.hpp"
#include "render3d_terrain.hpp"

// Iteration 4: the CMake shader step must have produced valid SPIR-V for the
// re-authored triangle shaders. Pure file check -- no GPU device needed.
static bool readMagic(const char* path, uint32_t& magic, size_t& size) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  bool ok = n > 4 && fread(&magic, 4, 1, f) == 1;
  size = ok ? (size_t)n : 0;
  fclose(f);
  return ok;
}

static void testShaderArtifacts() {
#if defined(RENDER3D_VERT_SPV) && defined(RENDER3D_FRAG_SPV)
  const uint32_t kSpirvMagic = 0x07230203u;
  uint32_t m = 0;
  size_t n = 0;
  assert(readMagic(RENDER3D_VERT_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
  assert(readMagic(RENDER3D_FRAG_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
  assert(readMagic(RENDER3D_QUAD_VERT_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
  assert(readMagic(RENDER3D_QUAD_FRAG_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
  assert(readMagic(RENDER3D_MESH_VERT_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
  assert(readMagic(RENDER3D_MESH_FRAG_SPV, m, n) && m == kSpirvMagic);
  assert(n >= 20);
#endif
}

// Iteration 6: BuildMeshA flattens an .obj face into 3 layout-A verts with a
// unit-extent position box, a face normal, and texture-normalized UVs.
static void testMeshA() {
  obj::Model mo;
  mo.verts = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
  obj::Face f;
  f.v[0] = 0; f.v[1] = 1; f.v[2] = 2;
  f.uv[0] = 0; f.uv[1] = 0; f.uv[2] = 4; f.uv[3] = 0; f.uv[4] = 0; f.uv[5] = 4;
  mo.faces = {f};
  auto va = render3d::BuildMeshA(mo, 8.f, 8.f);
  assert(va.size() == 3);
  // x span 10 -> inv 1/10, centered at (5,5,0).
  assert(std::fabs(va[0].px + 0.5f) < 1e-5f && std::fabs(va[0].py + 0.5f) < 1e-5f);
  assert(std::fabs(va[1].px - 0.5f) < 1e-5f && std::fabs(va[1].py + 0.5f) < 1e-5f);
  assert(std::fabs(va[2].px + 0.5f) < 1e-5f && std::fabs(va[2].py - 0.5f) < 1e-5f);
  // Face normal = +Z; flat-shaded (all 3 identical).
  assert(std::fabs(va[0].nz - 1.f) < 1e-5f && std::fabs(va[1].nz - 1.f) < 1e-5f);
  // UV: pixel coords / 8.
  assert(std::fabs(va[0].u) < 1e-6f && std::fabs(va[0].v) < 1e-6f);
  assert(std::fabs(va[1].u - 0.5f) < 1e-5f && std::fabs(va[1].v) < 1e-6f);
  assert(std::fabs(va[2].u) < 1e-6f && std::fabs(va[2].v - 0.5f) < 1e-5f);
  // Untextured -> zero UVs, still 3 verts.
  auto v0 = render3d::BuildMeshA(mo, 0.f, 0.f);
  assert(v0.size() == 3 && v0[0].u == 0.f && v0[0].v == 0.f);
  // Empty model -> empty.
  obj::Model empty;
  assert(render3d::BuildMeshA(empty, 1.f, 1.f).empty());
}

// Iteration 5: layout-B quad packing -- 4 verts + a 4-index triangle strip,
// matching the fixed 0064b350 buffer sizes and the 00649a30 strip topology.
static void testQuad() {
  using namespace render3d;
  QuadVerts q = MakeQuad(10, 20, 200, 100);
  assert(q.v[0].x == 10.f && q.v[0].y == 20.f && q.v[0].u == 0.f && q.v[0].v == 0.f);
  assert(q.v[3].x == 210.f && q.v[3].y == 120.f && q.v[3].u == 1.f && q.v[3].v == 1.f);
  assert(q.v[0].depth == 0.5f && q.v[0].alpha == 1.f);
  assert(q.idx[0] == 0 && q.idx[1] == 1 && q.idx[2] == 2 && q.idx[3] == 3);
  assert(sizeof q.v == kFixedQuadVbBytes && sizeof q.idx == kFixedQuadIbBytes);
}

static bool eq(float a, float b) { return fabsf(a - b) < 1e-4f; }

// Mat4 compare. The original's mul sums terms in compiler-scheduled order; our
// natural k=0..3 order can differ in the last float bit, hence the epsilon.
static bool meq(const render3d::Mat4& a, const render3d::Mat4& b,
                float eps = 1e-5f) {
  for (int i = 0; i < 16; ++i)
    if (fabsf(a.m[i] - b.m[i]) > eps) return false;
  return true;
}

static void testMatrixMath() {
  using render3d::Mat4;
  using render3d::Identity;

  // Identity is neutral for Mul (00598dc0).
  Mat4 I = Identity();
  assert(meq(render3d::Mul(I, I), I));

  // Translation composes additively for row-vector matrices: T(a)*T(b)=T(a+b).
  Mat4 T1 = render3d::MatTranslate(1, 2, 3);
  Mat4 T2 = render3d::MatTranslate(4, 5, 6);
  assert(meq(render3d::Mul(T1, T2), render3d::MatTranslate(5, 7, 9)));
  // Translation lives in the last ROW (row-vector convention).
  assert(eq(T1.m[12], 1) && eq(T1.m[13], 2) && eq(T1.m[14], 3) &&
         eq(T1.m[3], 0) && eq(T1.m[7], 0) && eq(T1.m[11], 0));

  // Mul4/Mul5 are left-to-right chains (00598860/00598460 operation order).
  Mat4 A = render3d::MatTranslate(1, 2, 3);
  Mat4 B = render3d::WorldMatrix(0, 0, 0, 0, 0, 90);  // M5 = Rz(-90)
  Mat4 C = render3d::MatTranslate(0, 1, 0);
  Mat4 D = render3d::WorldMatrix(0, 0, 0, 0, 90, 0);  // M3 = Rx(-90)
  assert(meq(render3d::Mul4(A, B, C, D),
             render3d::Mul(render3d::Mul(render3d::Mul(A, B), C), D)));
  assert(meq(render3d::Mul5(A, B, C, D, I),
             render3d::Mul(render3d::Mul4(A, B, C, D), I)));

  // Multiplication is NOT commutative.
  Mat4 AB = render3d::Mul(A, B);
  Mat4 BA = render3d::Mul(B, A);
  assert(!meq(AB, BA));

  // Transpose (00650380) is an involution and moves translation row -> column.
  Mat4 M = render3d::WorldMatrix(1, 2, 3, 10, 20, 30);
  assert(meq(render3d::Transpose(render3d::Transpose(M)), M));
  Mat4 Mt = render3d::Transpose(render3d::MatTranslate(1, 2, 3));
  assert(eq(Mt.m[3], 1) && eq(Mt.m[7], 2) && eq(Mt.m[11], 3) &&
         eq(Mt.m[12], 0) && eq(Mt.m[13], 0) && eq(Mt.m[14], 0));

  // Constants (DAT_006d0528 / DAT_006d0534).
  assert(eq(render3d::kModelScale, 1.0f / 180.0f));
  assert(fabsf(render3d::kDeg2Rad - 0.017453292f) < 1e-8f);

  // 00599d40, zero rotation -> pure translation (mode 0 and mode 1).
  Mat4 W0 = render3d::WorldMatrix(1, 2, 3, 0, 0, 0, 0);
  assert(meq(W0, render3d::MatTranslate(1, 2, 3)));
  assert(meq(render3d::WorldMatrix(1, 2, 3, 0, 0, 0, 1),
             render3d::MatTranslate(1, 2, 3)));

  // 00599d40 sample, hand-derived from the decompile:
  //   pos=(1,2,3), rx=90, ry=rz=0, mode 0 -> M5*M4*M3*M2, M5=M3=I,
  //   M4 = Ry(90) = [0,0,-1,0; 0,1,0,0; 1,0,0,0; 0,0,0,1], so out = M4*T.
  Mat4 exp{{0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 2, 3, 1}};
  assert(meq(render3d::WorldMatrix(1, 2, 3, 90, 0, 0, 0), exp));

  // Same sample about rz=90 -> M5 = Rz(-90) = [0,-1,0,0; 1,0,0,0; ...], out=M5*T.
  Mat4 expz{{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1}};
  assert(meq(render3d::WorldMatrix(1, 2, 3, 0, 0, 90, 0), expz));

  // Parent matrix is left-multiplied into the chain (00598460 path).
  Mat4 P = render3d::MatTranslate(10, 0, 0);
  assert(meq(render3d::WorldMatrix(1, 2, 3, 90, 0, 0, 0, &P),
             render3d::Mul(P, render3d::WorldMatrix(1, 2, 3, 90, 0, 0, 0))));
}

// Format byte size (only the formats used by 0064b560's descriptors).
static uint32_t fmtSize(SDL_GPUVertexElementFormat f) {
  switch (f) {
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT: return 4;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2: return 8;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3: return 12;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4: return 16;
    case SDL_GPU_VERTEXELEMENTFORMAT_UINT: return 4;
    default: assert(false && "unexpected format"); return 0;
  }
}

// Every attribute must land inside its buffer slot's stride, and its slot must
// have a buffer description.
static void checkLayout(const SDL_GPUVertexBufferDescription* d, size_t dn,
                        const SDL_GPUVertexAttribute* a, size_t an) {
  for (size_t i = 0; i < an; ++i) {
    const SDL_GPUVertexAttribute& at = a[i];
    const SDL_GPUVertexBufferDescription* bd = nullptr;
    for (size_t j = 0; j < dn; ++j)
      if (d[j].slot == at.buffer_slot) bd = &d[j];
    assert(bd && "attribute references missing buffer slot");
    assert(at.offset + fmtSize(at.format) <= bd->pitch &&
           "attribute overruns vertex stride");
  }
}

static void testLayouts() {
  using namespace render3d;

  // Vertex-type strides/offsets (compile-time static_asserts in the header too).
  assert(sizeof(VertexA) == 0x20 && sizeof(VertexF) == 0x24 &&
         sizeof(VertexB) == 0x18 && sizeof(VertexC) == 4 &&
         sizeof(InstanceE) == 0x40);

  // 0064b560 descriptor tables: element count + stride + instance rate.
  assert(sizeof kAttrA / sizeof *kAttrA == 3 && kDescA[0].pitch == 0x20);
  assert(sizeof kAttrF / sizeof *kAttrF == 4 && kDescF[0].pitch == 0x24);
  assert(sizeof kAttrE / sizeof *kAttrE == 7 && kDescE[1].pitch == 0x40);
  assert(kDescE[1].input_rate == SDL_GPU_VERTEXINPUTRATE_INSTANCE);
  assert(kDescE[0].input_rate == SDL_GPU_VERTEXINPUTRATE_VERTEX);
  assert(sizeof kAttrB / sizeof *kAttrB == 4 && kDescB[0].pitch == 0x18);
  assert(sizeof kAttrD / sizeof *kAttrD == 4 && kDescD[0].pitch == 0x18);
  assert(sizeof kAttrC / sizeof *kAttrC == 1 && kDescC[0].pitch == 0x4);

  checkLayout(kDescA, 1, kAttrA, 3);
  checkLayout(kDescF, 1, kAttrF, 4);
  checkLayout(kDescE, 2, kAttrE, 7);
  checkLayout(kDescB, 1, kAttrB, 4);
  checkLayout(kDescD, 1, kAttrD, 4);
  checkLayout(kDescC, 1, kAttrC, 1);

  // 0064a790 constant-buffer table: sizes in create order, VS/PS bind slots.
  assert(kConstantBuffers[0].size == 0x50 && kConstantBuffers[0].vs_slot == 0);
  assert(kConstantBuffers[1].size == 0x40 && kConstantBuffers[1].vs_slot == 1);
  assert(kConstantBuffers[2].size == 0x40 && kConstantBuffers[2].vs_slot == 2);
  assert(kConstantBuffers[3].size == 0x40 && kConstantBuffers[3].vs_slot == 3 &&
         kConstantBuffers[3].ps_slot == 3);
  assert(kConstantBuffers[4].size == 0x10 && kConstantBuffers[4].vs_slot == 4);
  assert(kConstantBuffers[5].size == 0x50 && kConstantBuffers[5].vs_slot == 5);
  assert(kConstantBuffers[6].size == 0x40 && kConstantBuffers[6].ps_slot == 6 &&
         kConstantBuffers[6].vs_slot == -1);
  assert(kConstantBuffers[7].size == 0x600 && kConstantBuffers[7].vs_slot == 7);

  // 0064b350 fixed buffers.
  assert(kFixedQuadVbBytes == 0x60 && kFixedQuadIbBytes == 0x8 &&
         kFixedRectIbBytes == 0x4 && kFixedTriVbBytes == 0xc);
  assert(kFixedQuadVbBytes / kStrideB == 4);  // 4 layout-B quad verts
  assert(kFixedTriVbBytes / kStrideC == 3);   // 3 layout-C point verts

  // 006487f0 terrain append round-trip.
  VertexB v = MakeTerrainVertex(3, 4, 0.25f, 0.75f, 0.5f, 0.8f);
  assert(v.x == 3.f && v.y == 4.f && v.u == 0.25f && v.v == 0.75f &&
         v.depth == 0.5f && v.alpha == 0.8f);
}

// Iteration 7: terrain cell cull (0040e260) + layout-B append (006487f0).
static void testTerrainCull() {
  using namespace render3d;
  int32_t pos[9] = {0, 0, -10, 10, 0, -10, 0, 10, -10};
  int32_t v[3] = {0, 1, 2};
  int32_t n[3];
  assert(CellNormal(pos, v, n));
  // AB=(10,0,0), AC=(0,10,0) -> n=(0,0,100); nz>0 -> culled.
  assert(n[0] == 0 && n[1] == 0 && n[2] == 100);
  assert(!CellVisible(pos, v));
  // Reverse winding -> nz<0 -> visible.
  int32_t v2[3] = {0, 2, 1};
  assert(CellNormal(pos, v2, n) && n[2] == -100);
  assert(CellVisible(pos, v2));
  // Degenerate (zero area) -> neither.
  int32_t flat[9] = {5, 5, -10, 5, 5, -10, 5, 5, -10};
  assert(!CellNormal(flat, v, n) && !CellVisible(flat, v));
  // z below -701 -> skipped.
  int32_t deep[9] = {0, 0, -800, 10, 0, -800, 0, 10, -800};
  assert(!CellVisible(deep, v2));
  // Big normal (>1000) takes the normalise branch; the nz sign still decides.
  int32_t big[9] = {0, 0, -10, 2000, 0, -10, 0, 2000, -10};
  assert(!CellVisible(big, v));   // nz = +4e6
  assert(CellVisible(big, v2));   // nz = -4e6
}

static void testTerrainAppend() {
  using namespace render3d;
  int32_t pos[9] = {0, 0, -10, 30, 0, -10, 0, 20, -10};
  CellFace f{};
  f.v[0] = 0; f.v[1] = 1; f.v[2] = 2;
  f.material = 1;
  f.uv[0] = 0; f.uv[1] = 0; f.uv[2] = 64; f.uv[3] = 0; f.uv[4] = 0; f.uv[5] = 32;
  int32_t uvTab[4] = {1, 64, 32, 8};  // material 1 -> 64
  std::vector<VertexB> batch;
  bool full = TerrainAppend(batch, 6, pos, f, uvTab, 0.8f);
  assert(batch.size() == 3 && !full);
  assert(batch[0].x == 0.f && batch[0].y == 0.f);
  assert(batch[1].x == 30.f && batch[1].y == 0.f);
  assert(batch[2].x == 0.f && batch[2].y == 20.f);
  assert(eq(batch[0].u, 0.f) && eq(batch[2].v, 32.f / 64.f));
  assert(eq(batch[0].depth, kCellDepth) && eq(batch[0].alpha, 0.8f));
  // Second append reaches the cap -> flush signal.
  full = TerrainAppend(batch, 6, pos, f, uvTab, 0.8f);
  assert(batch.size() == 6 && full);
  // Already full -> still signals flush, no growth (006487f0 returns 2).
  full = TerrainAppend(batch, 6, pos, f, uvTab, 0.8f);
  assert(batch.size() == 6 && full);
}

// Iteration 7: CB0 (00650910) / CB1 (0064f470) packing.
static void testCameraCbs() {
  using namespace render3d;
  Mat4 a = MatTranslate(1, 2, 3);
  float cb[20];
  BuildCb0(a, 0.f, 48.f, 800.f, 600.f, 1600.f, 1200.f, 800.f, 600.f, 0.1f, 0.2f,
            true, cb);
  // transpose moves the translation row -> column.
  assert(eq(cb[3], 1) && eq(cb[7], 2) && eq(cb[11], 3));
  assert(eq(cb[12], 0) && eq(cb[13], 0) && eq(cb[14], 0));
  assert(eq(cb[16], 0.f));               // 2*offX/scrW
  assert(eq(cb[17], 2.f * 48.f / 600.f));  // 0.16
  assert(eq(cb[18], 0.1f) && eq(cb[19], 0.2f));
  // letterbox off -> offsets scaled by win/rtTarget.
  BuildCb0(a, 0.f, 48.f, 800.f, 600.f, 1600.f, 1200.f, 800.f, 600.f, 0.f, 0.f,
            false, cb);
  assert(eq(cb[17], (1200.f / 600.f) * (2.f * 48.f / 600.f)));
  float cb1[16];
  BuildCb1(MatTranslate(4, 5, 6), cb1);
  assert(eq(cb1[3], 4) && eq(cb1[7], 5) && eq(cb1[11], 6));
}

static void testSyntheticGrid() {
  using namespace render3d;
  auto v = SyntheticGridVerts(3, 3, 200.f, 200.f);
  assert(v.size() == 24u);  // 2x2 cells * 2 tris * 3 verts
  assert(eq(v[0].x, 0.f) && eq(v[0].y, 0.f) && eq(v[0].u, 0.f) && eq(v[0].v, 0.f));
  assert(SyntheticGridVerts(1, 3, 200.f, 200.f).empty());
}

// Iteration 8: world-space lake scene (DEPTHMAP -> layout-A mesh + camera).
static void testScene() {
  using namespace render3d;
  // Height table endpoints.
  assert(eq(SceneHeight(0), 0.f));
  assert(SceneHeight(255) > 0.9f);

  ves::Grid g;
  g.w = 3; g.h = 3;
  g.cells = {0, 0, 0, 0, 255, 0, 0, 0, 0};
  g.pal.resize(768, 0);
  auto m = BuildTerrainMesh(g, 20.f);
  assert(m.size() == 2u * 2u * 6u);
  // The center (255) cell dips by ~1.0*20; corners stay at y=0.
  float minY = 0.f, maxY = 0.f;
  for (auto& v : m) { if (v.py < minY) minY = v.py; if (v.py > maxY) maxY = v.py; }
  assert(minY < -15.f && minY >= -20.001f);
  assert(maxY == 0.f);
  // World x/z span +-128.
  float minX = 1e9f, maxX = -1e9f;
  for (auto& v : m) { if (v.px < minX) minX = v.px; if (v.px > maxX) maxX = v.px; }
  assert(eq(minX, -128.f) && eq(maxX, 128.f));
  // Normals are unit length.
  for (auto& v : m) assert(fabsf(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz - 1.f) < 1e-4f);

  // Texture is padded to the 256-byte row pitch, h rows, RGBA.
  auto tex = BuildTerrainTex(g);
  assert(tex.size() == (size_t)TexWidthFor(3) * 3u * 4u);

  // Degenerate grid -> empty.
  ves::Grid e;
  assert(BuildTerrainMesh(e).empty());

  // Camera: eye on +z looking at origin. The origin must project to clip
  // x~0, and a point between near/far to z in 0..1.
  SceneCam c;
  c.ex = 0; c.ey = 0; c.ez = 10; c.tx = c.ty = c.tz = 0;
  Mat4 vp = ViewProj(c, 1.f);
  auto xf = [&](float x, float y, float z, float out[4]) {
    float v[4] = {x, y, z, 1};
    for (int i = 0; i < 4; ++i) {
      out[i] = 0;
      for (int j = 0; j < 4; ++j) out[i] += v[j] * vp.m[j * 4 + i];
    }
  };
  float o[4];
  xf(0, 0, 0, o);  // origin, on-axis
  assert(fabsf(o[0]) < 1e-4f);
  assert(fabsf(o[1]) < 1e-4f);
  assert(o[2] / o[3] > 0.f && o[2] / o[3] < 1.f);  // in the depth range
  xf(0, 5, 0, o);  // world +y is up -> clip y positive (NDC y-up)
  assert(o[1] / o[3] > 0.f);
}

int main() {
  // 16:9 target in a 4:3 window -> full width, letterbox top/bottom.
  render3d::Rect r = render3d::Letterbox(800, 600, 16.f / 9.f);
  assert(eq(r.x, 0) && eq(r.w, 800));
  assert(eq(r.h, 450) && eq(r.y, 75));

  // Square target in a 4:3 window -> full height, pillarbox left/right.
  r = render3d::Letterbox(800, 600, 1.f);
  assert(eq(r.y, 0) && eq(r.h, 600));
  assert(eq(r.w, 600) && eq(r.x, 100));

  // Exact fit -> whole window.
  r = render3d::Letterbox(1600, 900, 16.f / 9.f);
  assert(eq(r.x, 0) && eq(r.y, 0) && eq(r.w, 1600) && eq(r.h, 900));

  // Wider window than target -> pillarbox.
  r = render3d::Letterbox(1000, 500, 4.f / 3.f);
  assert(eq(r.w, 500.f * 4.f / 3.f) && eq(r.h, 500) && eq(r.x, 1000.f / 2 - r.w / 2));

  // Degenerate -> empty, no crash / no negative sizes.
  r = render3d::Letterbox(0, 0, 16.f / 9.f);
  assert(eq(r.w, 0) && eq(r.h, 0));
  r = render3d::Letterbox(800, 600, 0.f);
  assert(eq(r.w, 0) && eq(r.h, 0));
  r = render3d::Letterbox(-5, 600, 1.f);
  assert(eq(r.w, 0) && eq(r.h, 0));

  assert(eq(render3d::ClearGrey(), 0.75f));

  testMatrixMath();
  testLayouts();
  testShaderArtifacts();
  testQuad();
  testMeshA();
  testTerrainCull();
  testTerrainAppend();
  testCameraCbs();
  testSyntheticGrid();
  testScene();

  printf("render3d tests pass\n");
  return 0;
}
