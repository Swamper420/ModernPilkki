// Walk grid rule (004af230) + click-order decision (0047f530 / 00476c00),
// clean-room port from documented behavior. Pure C++17, no SDL.
// Full evidence trail: docs/sim-walk.md.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace walkgrid {

inline constexpr double kCell = 600.0;  // world cell (004b8b20: lake+0x1516c/0x15170 = 600)
inline constexpr int kPassBit = 0x10;   // corner byte: bit CLEAR = passable corner
inline constexpr int kMid = 300;        // half-cell split inside the mask rule
inline constexpr double kMinClickDist = 1500.0;  // 00799154
inline constexpr double kStandOff = 90.0;        // 006da098 behind-hole stand-off

// Corner-byte grid: one byte per grid point, row-major [y*w+x]. The
// original keeps it at lake+0x15168 -> {*obj: +0x500 data, +0x504 w,
// +0x508 h} (004af230). Only bit 0x10 is used here; 0x20 gates the ground
// height lookup (004b15d0).
struct GridView {
  const uint8_t *data = nullptr;
  int w = 0, h = 0;

  // 004af230 clamps each sampled corner to [0,w-1]/[0,h-1].
  uint8_t At(int cx, int cy) const {
    if (cx < 0) cx = 0;
    if (cx > w - 1) cx = w - 1;
    if (cy < 0) cy = 0;
    if (cy > h - 1) cy = h - 1;
    return data[(size_t)cy * w + cx];
  }
};

// The 16-case corner-mask rule of 004af230 (jump table at 0x004af4d4).
// Bit 8 = corner (0,0), bit 4 = (600,0), bit 2 = (600,600), bit 1 =
// (0,600); a mask bit is set when that corner's 0x10 bit is CLEAR
// (passable). u,v = position inside the cell (0..599 each). Walkable iff
// the point falls on the passable side of the corner split: straight
// half-cell splits for 2/3/6/9/12/15-corner masks, triangles for
// single-corner masks, the u+v or v-u diagonals for the mixed pairs,
// and opposite-corner pairs (5, 10) and empty (0) are never walkable.
inline bool MaskWalkable(int mask, int u, int v) {
  switch (mask & 15) {
    case 1:
      return u <= kMid && v >= kMid && (u - v + 600) < kMid;
    case 2:
      return u >= kMid && v >= kMid && (u + v) > 900;
    case 3:
      return v > kMid;
    case 4:
      return u >= kMid && v <= kMid && (v - u + 600) < kMid;
    case 6:
      return u > kMid;
    case 7:
      return u > kMid || v > kMid || (u + v) >= kMid;
    case 8:
      return u < kMid && v < kMid && (u + v) < kMid;
    case 9:
      return u < kMid;
    case 11:
      return u < kMid || v > kMid || (v - u + 600) >= kMid;
    case 12:
      return v < kMid;
    case 13:
      return u < kMid || v < kMid || (u + v) <= 900;
    case 14:
      return u > kMid || v < kMid || (u - v + 600) >= kMid;
    case 15:
      return true;
    default:  // 0, 5, 10
      return false;
  }
}

// 004af230 walkability at a world position.
inline bool Walkable(const GridView &g, double x, double y) {
  if (!g.data || g.w <= 0 || g.h <= 0) return false;
  // ponytail: cells outside the grid are hard-blocked; the world ends at
  // (w-1)*600 x (h-1)*600 (004b8b20 writes those extents to lake
  // +0x15160/+0x15164). 004af230 itself clamps the corner samples to the
  // edge and extrapolates the u/v split, so a literal port would let a
  // far-out point inherit the edge corners; blocking OOB instead matches
  // the "can't walk off the lake" wall the click path relies on. Upgrade
  // to clamp-extrapolation only if a lake ever needs it.
  if (x < 0 || y < 0) return false;
  int cx = (int)(x / kCell);
  int cy = (int)(y / kCell);
  if (cx >= g.w || cy >= g.h) return false;
  int u = (int)(x - cx * kCell);
  int v = (int)(y - cy * kCell);
  int mask = ((g.At(cx, cy) & kPassBit) ? 0 : 8) |
             ((g.At(cx + 1, cy) & kPassBit) ? 0 : 4) |
             ((g.At(cx + 1, cy + 1) & kPassBit) ? 0 : 2) |
             ((g.At(cx, cy + 1) & kPassBit) ? 0 : 1);
  return MaskWalkable(mask, u, v);
}

// ---- click-order decision (0047f530 order entry + 00476c00 snap) ----

enum { kReject = 0, kWalk = 1, kHoleSnap = 2 };

// Hole record as the click path sees it: position plus the busy flag
// (record +0x10 read by 004af1b0; nonzero = someone is fishing there).
struct HoleView {
  double x = 0, y = 0;
  bool busy = false;
};

struct ClickOrder {
  int kind = kReject;
  double tx = 0, ty = 0;
};

// Verified semantics (docs/sim-walk.md section 1): 0047f530 rejects only
// negative coordinates -- an out-of-bounds click (x,y >= 0) is ACCEPTED,
// never clamped; the walk grid then blocks progress mid-walk. The 1500
// minimum click distance (00799154) is skipped when already arrived
// (+0x698). A click near an open hole snaps to the 90-unit stand-off
// point on the approach side (00476c00: hole + unit(pos-hole) * 90);
// fbX/fbY is the fallback direction when standing exactly on the hole.
inline ClickOrder Decide(double wx, double wy, double px, double py,
                         bool arrived, double minDist, const HoleView *hole,
                         double fbX = -1.0, double fbY = 0.0) {
  ClickOrder o;
  if (wx < 0 || wy < 0) return o;        // 0047f530 negative guard
  if (hole && hole->busy) return o;      // 004af1b0 != 0: order dropped
  if (hole) {
    o.kind = kHoleSnap;
    double ux = px - hole->x, uy = py - hole->y;
    double l = std::sqrt(ux * ux + uy * uy);
    if (l < 1e-9) {
      ux = fbX;
      uy = fbY;
      l = std::sqrt(ux * ux + uy * uy);
      if (l < 1e-9) l = 1.0;
    }
    o.tx = hole->x + ux / l * kStandOff;
    o.ty = hole->y + uy / l * kStandOff;
    return o;
  }
  double dx = wx - px, dy = wy - py;
  if (!arrived && dx * dx + dy * dy < minDist * minDist) return o;  // 00799154
  o.kind = kWalk;
  o.tx = wx;
  o.ty = wy;
  return o;
}

}  // namespace walkgrid
