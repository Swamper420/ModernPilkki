// Iteration 10 of the SDL_GPU port: the lake's static objects (objpos) as 3D
// models -- the it8 stand-in for 0040afd0 (draw one .obj instance). The original
// walks the world object list each frame; here the placements are static, so
// every instance of a model is baked (world-transformed) into one layout-A
// vertex list once, at init, and drawn with the shared view-projection.
//
// Placement: FUN_004b6d70 stores rec x/y/z RAW (no fixed-point shift) in
// game units (600 per grid cell); they span 0..600*GridW. They map to the +-128
// world frame with the same transform as game::PortX/PortY, but using the
// terrain mesh's (w-1) division so props line up with the rendered bed. yaw/size
// come from objects.dat (Angle/Scale); non-`gfx/...` classes (<automatic>
// footprints/holes) are skipped.
//
// ponytail: yaw only, and the model is lifted by half its max extent so it sits
// on the stand-in bed -- exact height/rotation come from the objpos tail and
// 0040afd0's transform, still open (docs/render.md 13).
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "objects.hpp"
#include "render3d_math.hpp"
#include "render3d_mesh.hpp"
#include "render3d_scene.hpp"
#include "ves.hpp"

namespace render3d {

// game::kCell -- 600-unit grid cell (docs/sim.md delta 3).
inline constexpr float kCell = 600.f;

struct PropInstance {
  float x = 0, z = 0;  // world +-128 frame
  float yaw = 0;       // objects.dat Angle (degrees)
  float scale = 1;     // objects.dat Scale multiplier
  int cls = 0;
  std::string file;    // gfx/obj/... path ("" = unmapped)
};

// Mapped objpos records -> placements, in group/record order. `depth` supplies
// the grid dimensions for the game-units -> +-128 mapping.
inline std::vector<PropInstance> PropInstances(
    const std::vector<ves::ObjGroup>& groups, const objects::ObjTable* tab,
    const ves::Grid& depth) {
  std::vector<PropInstance> out;
  if (!tab || depth.w < 2 || depth.h < 2) return out;
  const float sx = 256.f / (kCell * (float)(depth.w - 1));
  const float sz = 256.f / (kCell * (float)(depth.h - 1));
  for (const auto& g : groups)
    for (const auto& rec : g) {
      if (rec.cls >= tab->rows.size()) continue;
      const objects::ObjDef& row = tab->rows[rec.cls];
      if (row.file.compare(0, 3, "gfx") != 0) continue;  // <automatic>/disabled
      PropInstance p;
      p.x = (float)rec.x * sx - 128.f;  // game units -> +-128 world frame
      p.z = (float)rec.y * sz - 128.f;
      p.yaw = (float)row.angle;
      p.scale = (float)(row.scale > 0 ? row.scale : 1.0);
      p.cls = rec.cls;
      p.file = row.file;
      out.push_back(p);
    }
  return out;
}

// Bake one model's instances into a world-space layout-A list. `base` is the
// model normalised to a unit box (BuildMeshA); instances scale it back by
// extent/180 (DAT_006d0528) times the objects.dat multiplier and place it on the
// terrain surface (TerrainHeightAt).
inline std::vector<VertexA> BakeProps(
    const obj::Model& mo, const std::vector<PropInstance>& inst,
    const ves::Grid& depth, float zScale, float texW, float texH) {
  const std::vector<VertexA> base = BuildMeshA(mo, texW, texH);
  std::vector<VertexA> out;
  if (base.empty()) return out;
  const float ext = MeshExtent(mo);
  const float unit = ext > 0 ? ext / 180.f : 1.f;  // 1/180 world units -> engine
  out.reserve(base.size() * inst.size());
  for (const auto& in : inst) {
    const float s = unit * in.scale;
    const float gy = TerrainHeightAt(depth, in.x, in.z, zScale);
    const Mat4 W = Mul(MatScale(s),
                       Mul(MatRotY(in.yaw), MatTranslate(in.x, gy + s * 0.5f, in.z)));
    for (const VertexA& v : base) {
      VertexA o = v;
      float p[3], n[3];
      XformPoint(W, v.px, v.py, v.pz, p);
      XformDir(W, v.nx, v.ny, v.nz, n);
      o.px = p[0]; o.py = p[1]; o.pz = p[2];
      const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (nl > 0) { o.nx = n[0] / nl; o.ny = n[1] / nl; o.nz = n[2] / nl; }
      out.push_back(o);
    }
  }
  return out;
}

}  // namespace render3d
