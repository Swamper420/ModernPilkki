// Smallest assert: letterbox math (aspect preserved/centered/degenerate), the
// clear-grey factor, and the iteration-2 4x4 matrix math (render3d_math.hpp).
// No SDL window or GPU device needed.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "render3d.hpp"
#include "render3d_layout.hpp"
#include "render3d_math.hpp"

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
#endif
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

  printf("render3d tests pass\n");
  return 0;
}
