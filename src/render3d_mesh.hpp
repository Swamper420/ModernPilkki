// Iteration 6: flatten an .obj into layout-A (static mesh) vertices for GPU
// upload. Mirrors the original's per-sub-mesh stride-0x20 static buffers bound
// by 006489c0 with layout A (docs/render.md 3.1/5), except the normals are
// computed here: the .obj only stores faces, and 004070e0 builds the GPU
// vertex arrays with baked normals (per-face data isn't in the file).
//
// Per-face duplicated verts -> flat shading; positions are centered and
// scaled to a ~[-0.5,0.5] box so a pure rotation uniform fits the NDC cube.
// UVs are pixel coords (obj.hpp) normalized by the texture size; texW/texH
// <= 0 (untextured) -> UV (0,0).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "obj.hpp"
#include "render3d_layout.hpp"

namespace render3d {

// Largest bbox axis of an .obj in its own (world) units. BuildMeshA divides by
// this, so a baked instance restores real size by multiplying back by it.
inline float MeshExtent(const obj::Model& m) {
  if (m.verts.empty()) return 0.f;
  float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
  for (const auto& v : m.verts) {
    const float p[3] = {(float)v.x, (float)v.y, (float)v.z};
    for (int k = 0; k < 3; ++k) {
      if (p[k] < mn[k]) mn[k] = p[k];
      if (p[k] > mx[k]) mx[k] = p[k];
    }
  }
  float ext = mx[0] - mn[0];
  if (mx[1] - mn[1] > ext) ext = mx[1] - mn[1];
  if (mx[2] - mn[2] > ext) ext = mx[2] - mn[2];
  return ext;
}

inline std::vector<VertexA> BuildMeshA(const obj::Model& m, float texW,
                                       float texH) {
  std::vector<VertexA> out;
  if (m.verts.empty() || m.faces.empty()) return out;
  out.reserve((size_t)m.faces.size() * 3);

  float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
  for (const auto& v : m.verts) {
    const float p[3] = {(float)v.x, (float)v.y, (float)v.z};
    for (int k = 0; k < 3; ++k) {
      if (p[k] < mn[k]) mn[k] = p[k];
      if (p[k] > mx[k]) mx[k] = p[k];
    }
  }
  float ext = mx[0] - mn[0];
  if (mx[1] - mn[1] > ext) ext = mx[1] - mn[1];
  if (mx[2] - mn[2] > ext) ext = mx[2] - mn[2];
  const float inv = ext > 0 ? 1.0f / ext : 1.0f;
  const float c[3] = {(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f,
                      (mn[2] + mx[2]) * 0.5f};
  const bool hasTex = texW > 0 && texH > 0;

  for (const auto& f : m.faces) {
    const obj::Vert& a = m.verts[f.v[0]];
    const obj::Vert& b = m.verts[f.v[1]];
    const obj::Vert& d = m.verts[f.v[2]];
    // P2 .obj is y-DOWN with the origin at the model's base (verified on
    // nature/manty3.obj y[-500,0], buildings/koppi.obj floor at y=0,
    // cars/car1.obj roof at y=-257): world up = -model y, so edges and
    // positions are mirrored in y here (normals follow the mirrored edges).
    const float e1[3] = {(float)(b.x - a.x), -(float)(b.y - a.y),
                         (float)(b.z - a.z)};
    const float e2[3] = {(float)(d.x - a.x), -(float)(d.y - a.y),
                         (float)(d.z - a.z)};
    float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                  e1[2] * e2[0] - e1[0] * e2[2],
                  e1[0] * e2[1] - e1[1] * e2[0]};
    float len = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    if (len > 0) {
      len = 1.0f / sqrtf(len);
      n[0] *= len;
      n[1] *= len;
      n[2] *= len;
    } else {
      n[0] = 0;
      n[1] = 0;
      n[2] = 1;
    }
    // The y mirror reverses winding, so the edge cross points inward: negate
    // to keep the normal outward (shading uses |dot|, but stay correct).
    n[0] = -n[0];
    n[1] = -n[1];
    n[2] = -n[2];
    const obj::Vert* tri[3] = {&a, &b, &d};
    for (int k = 0; k < 3; ++k) {
      VertexA va{};
      va.px = ((float)tri[k]->x - c[0]) * inv;
      va.py = -((float)tri[k]->y - c[1]) * inv;  // y-down model -> y-up world
      va.pz = ((float)tri[k]->z - c[2]) * inv;
      va.nx = n[0];
      va.ny = n[1];
      va.nz = n[2];
      if (hasTex) {
        va.u = (float)(f.uv[2 * k] % (uint32_t)texW) / texW;
        va.v = (float)(f.uv[2 * k + 1] % (uint32_t)texH) / texH;
      }
      out.push_back(va);
    }
  }
  return out;
}

}  // namespace render3d
