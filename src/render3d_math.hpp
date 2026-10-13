// Iteration 2 of the SDL_GPU port of the PLAY-screen 3D scene: 4x4 matrix math.
// Ported 1:1 from the original (addresses are image VAs, all pinned from the
// Ghidra decompile + the raw .text disassembly -- see docs/render.md 6/10/11):
//
//   Mul / Mul4 / Mul5 <- 00598dc0 / 00598860 / 00598460 (row-major A*B / A*B*C*D
//                        / A*B*C*D*E; all three verified from the decompiled
//                        indexing and the exact operand/order of the call sites)
//   Transpose         <- 00650380 (transposes the CPU matrix into the VS constant
//                        buffer before UpdateSubresource)
//   WorldMatrix       <- 00599d40
//   constants         <- DAT_006d0528 (1/180), DAT_006d0534 (pi/180),
//                        DAT_007778e0 (pi)
//
// CONVENTION (pinned from the decompiled indexing of 00598dc0):
//   Row-major storage: element (row,col) = m[row*4 + col].
//   Row-vector convention (v' = v * M), so translation is the LAST ROW
//   (m[12],m[13],m[14]) -- not the last column. This is D3D's CPU-side layout.
//   00650380 transposes the whole matrix before the constant-buffer upload
//   (HLSL column-vector convention), i.e. Transpose() IS the GPU upload layout.
//   Clip space is D3D (z in 0..1, Y-down chosen by the viewport) -- no clip
//   matrix is built here; this file only builds world matrices.
//
// 00599d40 CORRECTION to docs/render.md 6: the three float arguments are ROTATION
// angles, NOT scale. Caller 0040b0b0 passes
//     (float)x,(float)y,(float)z, rot*DAT_006d0528(1/180), ..., out, parent,
//     *(model+0xd8)
// i.e. rotations in DEGREES; 00599d40 then multiplies each by DAT_007778e0 (pi),
// so degrees -> radians across the two functions (net deg*pi/180). Verified
// build (all from the disassembly, selector `mode` = model+0xd8, M2=Translation):
//   D0 = [sin(a4),cos(a4),sin(a5),cos(a5)]  (0x66e4d0, a4=[ebx+0x14], a5=[ebx+0x18])
//   R6 = [sin(a6),cos(a6)]                  (0x66e480, a6=[ebx+0x1c])
//   M3 = Rx(-a5) = [1,0,0,0; 0,c5,-s5,0; 0,s5,c5,0; 0,0,0,1]
//   M4 = Ry( a4) = [c4,0,-s4,0; 0,1,0,0; s4,0,c4,0; 0,0,0,1]
//   M5 = Rz(-a6) = [c6,-s6,0,0; s6,c6,0,0; 0,0,1,0; 0,0,0,1]
//   mode -> multiply order (null parent) / (with parent, prepended into the chain):
//     0: M5*M4*M3*M2   1: M2*M5*M4*M3   2: M4*M3*M5*M2   3: M2*M4*M3*M5
//     4/5: no-op (original leaves the out buffer untouched; we return Identity)
//     6: M2*M5*M3*M4   7: M3*M4*M5*M2
#pragma once

#include <cmath>

namespace render3d {

// DAT_006d0528: 1/180, used at the 00599d40 call site (summarised as "model
// scale" in the notes; here it is the degrees -> (radians/pi) factor).
constexpr float kModelScale = 1.0f / 180.0f;
// DAT_007778e0: pi, multiplied inside 00599d40 after kModelScale.
constexpr float kPi = 3.1415927410125732f;
// DAT_006d0534: pi/180. Net degrees->radians factor of the two-step conversion.
constexpr float kDeg2Rad = kModelScale * kPi;

// Row-major 4x4. m[row*4+col]; row-vector convention (v*M), translation last row.
struct Mat4 {
  float m[16];
};

inline Mat4 Identity() {
  return Mat4{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

// out = A*B  (00598dc0). C[i][j] = sum_k A[i][k]*B[k][j].
// The original's term order is compiler-scheduled; summation order here is
// natural k=0..3 (float rounding may differ by ~1e-7, see test comments).
inline Mat4 Mul(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.f;
      for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
      r.m[i * 4 + j] = s;
    }
  return r;
}

// 00598860: out = A*B*C*D, evaluated left-to-right (((A*B)*C)*D) exactly as the
// original (M1=A*B, M2=M1*C, out=M2*D).
inline Mat4 Mul4(const Mat4& a, const Mat4& b, const Mat4& c, const Mat4& d) {
  return Mul(Mul(Mul(a, b), c), d);
}

// 00598460: out = A*B*C*D*E, evaluated ((((A*B)*C)*D)*E).
inline Mat4 Mul5(const Mat4& a, const Mat4& b, const Mat4& c, const Mat4& d,
                 const Mat4& e) {
  return Mul(Mul(Mul(Mul(a, b), c), d), e);
}

// 00650380: element (row,col) -> (col,row). The original transposes into a
// stack copy before the D3D CB upload (HLSL column-vector semantics). SDL_GPU
// + our GLSL shaders instead read the row-major matrix correctly when it is
// pushed UNTRANSPOSED (verified: the scene camera only frames right without it),
// so do not apply this before PushVertexUniform.
inline Mat4 Transpose(const Mat4& a) {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r.m[i * 4 + j] = a.m[j * 4 + i];
  return r;
}

// Uniform scale about the origin (row-vector: v*S). Not part of 00599d40 (its
// third arg is rotation, see the header note); needed to restore a baked model's
// real size -- BuildMeshA normalises every mesh to a unit box (render3d_mesh.hpp).
inline Mat4 MatScale(float s) {
  return Mat4{{s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1}};
}

// Yaw about the vertical (engine +Y) axis, degrees -> the original's two-step
// factor (DAT_006d0528 * pi == pi/180). Matches 00599d40's M4 = Ry(a4).
inline Mat4 MatRotY(float deg) {
  const float a = deg * kDeg2Rad;
  const float c = cosf(a), s = sinf(a);
  return Mat4{{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1}};
}

// Row-vector transform of a point (w=1) / direction (w=0) by a row-major Mat4.
inline void XformPoint(const Mat4& m, float x, float y, float z, float out[3]) {
  out[0] = x * m.m[0] + y * m.m[4] + z * m.m[8] + m.m[12];
  out[1] = x * m.m[1] + y * m.m[5] + z * m.m[9] + m.m[13];
  out[2] = x * m.m[2] + y * m.m[6] + z * m.m[10] + m.m[14];
}
inline void XformDir(const Mat4& m, float x, float y, float z, float out[3]) {
  out[0] = x * m.m[0] + y * m.m[4] + z * m.m[8];
  out[1] = x * m.m[1] + y * m.m[5] + z * m.m[9];
  out[2] = x * m.m[2] + y * m.m[6] + z * m.m[10];
}

// Internal: translation with the last row (m[12..14]) = (x,y,z).
inline Mat4 MatTranslate(float x, float y, float z) {
  Mat4 r = Identity();
  r.m[12] = x;
  r.m[13] = y;
  r.m[14] = z;
  return r;
}

// 00599d40 world matrix. x,y,z are world position; rxDeg,ryDeg,rzDeg are the
// rotation triple in DEGREES (a4=rx->Y-plane, a5=ry->X-plane, a6=rz->Z-plane).
// `mode` is model+0xd8; `parent` (optional) is the matrix 0040b0b0 passes as the
// 8th argument and is left-multiplied into the chain. Degrees are converted with
// the original's two-step float rounding (deg*1/180 then *pi).
inline Mat4 WorldMatrix(float x, float y, float z, float rxDeg, float ryDeg,
                        float rzDeg, int mode = 0, const Mat4* parent = nullptr) {
  const float a4 = rxDeg * kModelScale * kPi;  // (rxDeg*kModelScale)*kPi
  const float a5 = ryDeg * kModelScale * kPi;
  const float a6 = rzDeg * kModelScale * kPi;
  const float s4 = sinf(a4), c4 = cosf(a4);
  const float s5 = sinf(a5), c5 = cosf(a5);
  const float s6 = sinf(a6), c6 = cosf(a6);

  const Mat4 m2 = MatTranslate(x, y, z);
  const Mat4 m3{{1, 0, 0, 0, 0, c5, -s5, 0, 0, s5, c5, 0, 0, 0, 0, 1}};  // Rx(-a5)
  const Mat4 m4{{c4, 0, -s4, 0, 0, 1, 0, 0, s4, 0, c4, 0, 0, 0, 0, 1}};  // Ry(a4)
  const Mat4 m5{{c6, -s6, 0, 0, s6, c6, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};  // Rz(-a6)

  const Mat4* o[4];
  switch (mode) {
    case 0: o[0] = &m5; o[1] = &m4; o[2] = &m3; o[3] = &m2; break;
    case 1: o[0] = &m2; o[1] = &m5; o[2] = &m4; o[3] = &m3; break;
    case 2: o[0] = &m4; o[1] = &m3; o[2] = &m5; o[3] = &m2; break;
    case 3: o[0] = &m2; o[1] = &m4; o[2] = &m3; o[3] = &m5; break;
    case 6: o[0] = &m2; o[1] = &m5; o[2] = &m3; o[3] = &m4; break;
    case 7: o[0] = &m3; o[1] = &m4; o[2] = &m5; o[3] = &m2; break;
    default: return Identity();  // ponytail: modes 4/5 are a no-op upstream
  }
  if (parent)
    return Mul5(*parent, *o[0], *o[1], *o[2], *o[3]);  // 00598460
  return Mul4(*o[0], *o[1], *o[2], *o[3]);             // 00598860
}

}  // namespace render3d
