// Iteration 7 of the SDL_GPU port: the camera constant buffers, ported 1:1
// from 00650910 (CB0) and the CB1 half of 0064f470. Pure data packing -- no GPU.
//
// 0064f470 caches two 4x4 matrices keyed by a camera id and uploads them:
//   param_2 -> globals 7c9570..7c95ac   (folded with the viewport by 00650910)
//   param_4 -> globals 7c9510..7c954c   -> VS CB1 (7c8bf8), transposed
// Both uploads are row-major CPU matrices written TRANSPOSED into the constant
// buffer (the same convention as 00650380). Verified from the store/upload
// order: CB1's 16 floats read back as param_4[0,4,8,c, 1,5,9,d, 2,6,a,e, 3,7,b,f].
//
// CB0 (0x50 bytes) = [ 4x4 = transpose(camA) | vec4 viewport ], where the
// viewport vector is
//   (2*offX/scrW, 2*offY/scrH, panX, panY)
// and the first two components are rescaled by (win/rtTarget) when the
// letterbox flag 7c8b08 is 0.
#pragma once

#include "render3d_math.hpp"

namespace render3d {

constexpr uint32_t kCb0Floats = 20;  // 0x50 bytes
constexpr uint32_t kCb1Floats = 16;  // 0x40 bytes

// 00650910: builds CB0. `camA` is the CPU matrix stored at 7c9570 (0064f470's
// param_2). offX/offY are the letterbox offsets (7c8b00/7c8b04), scrW/scrH the
// screen size (7c8b78/7c8b7c), pan the two runtime coords (7a6be0/7a6be4).
// When `letterbox` (7c8b08) is false the offsets are scaled by win/rtTarget.
inline void BuildCb0(const Mat4& camA, float offX, float offY, float scrW,
                     float scrH, float winW, float winH, float rtW, float rtH,
                     float panX, float panY, bool letterbox, float out[20]) {
  const Mat4 t = Transpose(camA);
  for (int i = 0; i < 16; ++i) out[i] = t.m[i];
  float vx = scrW != 0.f ? offX / scrW + offX / scrW : 0.f;
  float vy = scrH != 0.f ? offY / scrH + offY / scrH : 0.f;
  if (!letterbox) {
    if (rtW != 0.f) vx = (winW / rtW) * vx;
    if (rtH != 0.f) vy = (winH / rtH) * vy;
  }
  out[16] = vx;
  out[17] = vy;
  out[18] = panX;
  out[19] = panY;
}

// 0064f470 CB1: transpose(camB).
inline void BuildCb1(const Mat4& camB, float out[16]) {
  const Mat4 t = Transpose(camB);
  for (int i = 0; i < 16; ++i) out[i] = t.m[i];
}

}  // namespace render3d
