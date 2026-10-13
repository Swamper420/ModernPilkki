// Iteration 4 of the SDL_GPU port of the PLAY-screen 3D scene: the first real
// GPU pipeline. Loads two re-authored GLSL shaders (compiled to SPIR-V by
// glslc at build time) and creates one graphics pipeline, then draws a
// hardcoded triangle with no vertex buffer (positions come from gl_VertexID).
//
// This is scaffolding, not a port: it proves SPIR-V load + pipeline + render
// pass + draw + submit end to end. It mirrors the *shape* of the original
// pipeline init (0064a790, docs/render.md 2/3): one VS + PS, TRIANGLELIST, no
// vertex input, one colour target. The original's 6 layouts / 8 CBs / 5 depth
// states come in later iterations.
#pragma once

#include <SDL3/SDL_gpu.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "render3d_layout.hpp"
#include "render3d_mesh.hpp"

#include "pcx.hpp"

namespace render3d {

// Depth format for the world pass (it10): objects and terrain occlude correctly.
inline constexpr SDL_GPUTextureFormat kDepthFmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

// Original asset paths are Windows-style and keep the author's casing
// (`gfx\txt\misc\d_col.pcx` vs the shipped `D_col.pcx`). Normalise the
// separators, then resolve case by scanning the directory.
inline std::string NormPath(const std::string& p) {
  std::string o = p;
  for (auto& c : o)
    if (c == '\\') c = '/';
  return o;
}

inline std::string ResolveCase(const std::string& path) {
  if (FILE* f = fopen(path.c_str(), "rb")) {
    fclose(f);
    return path;
  }
  const size_t slash = path.find_last_of('/');
  const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  for (auto& c : base) c = (char)tolower((unsigned char)c);
  DIR* d = opendir(dir.c_str());
  if (!d) return path;
  std::string found = path;
  for (dirent* e; (e = readdir(d));) {
    std::string n = e->d_name, ln = n;
    for (auto& c : ln) c = (char)tolower((unsigned char)c);
    if (ln == base) {
      found = dir + "/" + n;
      break;
    }
  }
  closedir(d);
  return found;
}

// Whole-file read for assets (.obj/.pcx); empty on failure. Case-insensitive
// fallback so the original's mixed-case asset names still open on Linux.
inline std::vector<uint8_t> ReadWholeFile(const char* path) {
  std::vector<uint8_t> out;
  const std::string p = ResolveCase(NormPath(path));
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return out;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 0) {
    out.resize((size_t)n);
    if (fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
  }
  fclose(f);
  return out;
}

// .pcx -> tightly-packed RGBA8. Returns false (and clears rgba) on any failure.
inline bool DecodePcxRgba(const char* path, Uint32& w, Uint32& h,
                          std::vector<uint8_t>& rgba) {
  rgba.clear();
  w = h = 0;
  try {
    pcx::Image im = pcx::Decode(ReadWholeFile(path));
    w = (Uint32)im.w;
    h = (Uint32)im.h;
    size_t n = (size_t)im.w * im.h;
    rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
      uint8_t r, g, b, a = 255;
      if (im.planes == 1) {
        uint8_t k = im.idx[i];
        r = im.pal[3 * k];
        g = im.pal[3 * k + 1];
        b = im.pal[3 * k + 2];
      } else {
        size_t o = i * (im.planes == 3 ? 3 : 4);
        r = im.px[o];
        g = im.px[o + 1];
        b = im.px[o + 2];
        if (im.planes == 4) a = im.px[o + 3];
      }
      rgba[4 * i] = r;
      rgba[4 * i + 1] = g;
      rgba[4 * i + 2] = b;
      rgba[4 * i + 3] = a;
    }
    return true;
  } catch (const std::exception&) {
    w = h = 0;
    rgba.clear();
    return false;
  }
}

// Nearest/clamp sampler -- a stand-in; the original's 4 sampler objects
// (0064aa70) filter/address modes are not decoded yet (docs/render.md 13).
inline SDL_GPUSampler* MakeSampler(SDL_GPUDevice* dev) {
  SDL_GPUSamplerCreateInfo sci{};
  sci.min_filter = SDL_GPU_FILTER_NEAREST;
  sci.mag_filter = SDL_GPU_FILTER_NEAREST;
  sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  return SDL_CreateGPUSampler(dev, &sci);
}

// SPIR-V word 0 (little endian).
constexpr uint32_t kSpirvMagic = 0x07230203u;

// Reads a whole .spv file; empty vector on any failure.
inline std::vector<uint8_t> ReadSpirv(const char* path) {
  std::vector<uint8_t> out;
  FILE* f = fopen(path, "rb");
  if (!f) return out;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 4) {
    out.resize((size_t)n);
    if (fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
  }
  fclose(f);
  return out;
}

inline bool IsSpirv(const std::vector<uint8_t>& b) {
  if (b.size() < 4) return false;
  uint32_t m = 0;
  memcpy(&m, b.data(), 4);
  return m == kSpirvMagic;
}

struct Triangle {
  SDL_GPUShader* vs = nullptr;
  SDL_GPUShader* ps = nullptr;
  SDL_GPUGraphicsPipeline* pipe = nullptr;
  bool Ok() const { return pipe != nullptr; }
};

inline void ReleaseTriangle(SDL_GPUDevice* dev, Triangle& t);

inline SDL_GPUShader* MakeShader(SDL_GPUDevice* dev,
                                 const std::vector<uint8_t>& code,
                                 SDL_GPUShaderStage stage,
                                 Uint32 uniform_buffers = 0,
                                 Uint32 samplers = 0) {
  SDL_GPUShaderCreateInfo ci{};
  ci.code_size = code.size();
  ci.code = code.data();
  ci.entrypoint = "main";
  ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
  ci.stage = stage;
  ci.num_samplers = samplers;
  ci.num_storage_textures = 0;
  ci.num_storage_buffers = 0;
  ci.num_uniform_buffers = uniform_buffers;
  return SDL_CreateGPUShader(dev, &ci);
}

// Creates the triangle pipeline. false (no partial state leaked) on failure.
inline bool InitTriangle(SDL_GPUDevice* dev, SDL_GPUTextureFormat fmt,
                         const char* vertSpv, const char* fragSpv,
                         Triangle& t) {
  std::vector<uint8_t> v = ReadSpirv(vertSpv), f = ReadSpirv(fragSpv);
  if (!IsSpirv(v) || !IsSpirv(f)) {
    fprintf(stderr, "render3d: bad/missing SPIR-V (%s / %s)\n", vertSpv, fragSpv);
    return false;
  }
  t.vs = MakeShader(dev, v, SDL_GPU_SHADERSTAGE_VERTEX);
  t.ps = MakeShader(dev, f, SDL_GPU_SHADERSTAGE_FRAGMENT);
  if (!t.vs || !t.ps) {
    fprintf(stderr, "render3d: SDL_CreateGPUShader failed: %s\n", SDL_GetError());
    ReleaseTriangle(dev, t);
    return false;
  }

  SDL_GPUColorTargetDescription ct{};
  ct.format = fmt;

  SDL_GPUGraphicsPipelineCreateInfo ci{};
  ci.vertex_shader = t.vs;
  ci.fragment_shader = t.ps;
  ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  ci.target_info.color_target_descriptions = &ct;
  ci.target_info.num_color_targets = 1;

  t.pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
  if (!t.pipe) {
    fprintf(stderr, "render3d: SDL_CreateGPUGraphicsPipeline failed: %s\n",
            SDL_GetError());
    ReleaseTriangle(dev, t);
    return false;
  }
  return true;
}

inline void ReleaseTriangle(SDL_GPUDevice* dev, Triangle& t) {
  if (t.pipe) SDL_ReleaseGPUGraphicsPipeline(dev, t.pipe);
  if (t.vs) SDL_ReleaseGPUShader(dev, t.vs);
  if (t.ps) SDL_ReleaseGPUShader(dev, t.ps);
  t.pipe = nullptr;
  t.vs = nullptr;
  t.ps = nullptr;
}

// ---------------------------------------------------------------------------
// Iteration 5: layout-B textured quad (the 00648f60 / 00649a30 vocabulary).
// Adds the vertex/index buffer upload path (0064b350 fixed buffers,
// 0064dfe0/0064ce70 dynamic ones) and a sampler + texture (0064be00 upload).
// ---------------------------------------------------------------------------

struct Quad {
  SDL_GPUShader* vs = nullptr;
  SDL_GPUShader* ps = nullptr;
  SDL_GPUGraphicsPipeline* pipe = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUTexture* tex = nullptr;
  SDL_GPUBuffer* vb = nullptr;
  SDL_GPUBuffer* ib = nullptr;
  bool Ok() const { return pipe != nullptr; }
};

inline void ReleaseQuad(SDL_GPUDevice* dev, Quad& q);

// One-shot upload into a new GPU buffer. Self-contained: acquires its own
// command buffer + copy pass, so no frame loop is needed.
inline SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* dev,
                                   SDL_GPUBufferUsageFlags usage,
                                   const void* data, Uint32 size) {
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!cmd) return nullptr;
  SDL_GPUTransferBufferCreateInfo tci{};
  tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tci.size = size;
  SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tci);
  SDL_GPUBuffer* buf = nullptr;
  if (tb) {
    void* p = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (p) {
      memcpy(p, data, size);
      SDL_UnmapGPUTransferBuffer(dev, tb);
      SDL_GPUBufferCreateInfo bci{};
      bci.usage = usage;
      bci.size = size;
      buf = SDL_CreateGPUBuffer(dev, &bci);
    }
    if (buf) {
      SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTransferBufferLocation src{tb, 0};
      SDL_GPUBufferRegion dst{buf, 0, size};
      SDL_UploadToGPUBuffer(cp, &src, &dst, false);
      SDL_EndGPUCopyPass(cp);
    }
    SDL_ReleaseGPUTransferBuffer(dev, tb);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if (!buf)
    fprintf(stderr, "render3d: buffer upload failed: %s\n", SDL_GetError());
  return buf;
}

// One-shot upload of an RGBA8 image into a new 2D texture.
inline SDL_GPUTexture* UploadTexture2D(SDL_GPUDevice* dev, const void* rgba,
                                       Uint32 w, Uint32 h) {
  const Uint32 size = w * h * 4;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!cmd) return nullptr;
  SDL_GPUTransferBufferCreateInfo tci{};
  tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tci.size = size;
  SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tci);
  SDL_GPUTexture* tex = nullptr;
  if (tb) {
    void* p = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (p) {
      memcpy(p, rgba, size);
      SDL_UnmapGPUTransferBuffer(dev, tb);
      SDL_GPUTextureCreateInfo xci{};
      xci.type = SDL_GPU_TEXTURETYPE_2D;
      xci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      xci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
      xci.width = w;
      xci.height = h;
      xci.layer_count_or_depth = 1;
      xci.num_levels = 1;
      xci.sample_count = SDL_GPU_SAMPLECOUNT_1;
      tex = SDL_CreateGPUTexture(dev, &xci);
    }
    if (tex) {
      SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTextureTransferInfo src{tb, 0, w, h};
      SDL_GPUTextureRegion dst{tex, 0, 0, 0, 0, 0, w, h, 1};
      SDL_UploadToGPUTexture(cp, &src, &dst, false);
      SDL_EndGPUCopyPass(cp);
    }
    SDL_ReleaseGPUTransferBuffer(dev, tb);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if (!tex)
    fprintf(stderr, "render3d: texture upload failed: %s\n", SDL_GetError());
  return tex;
}

// Procedural 64x64 RGBA checker (scaffold; real .pcx/.spr upload via 0064be00
// comes with the sprite/texture iteration). Filter/address modes from 0064aa70
// are not yet decoded -- nearest/clamp is a stand-in.
inline std::vector<uint8_t> CheckerRgba(Uint32 w, Uint32 h) {
  std::vector<uint8_t> px(w * h * 4);
  for (Uint32 y = 0; y < h; ++y)
    for (Uint32 x = 0; x < w; ++x) {
      bool c = ((x / 8) + (y / 8)) & 1;
      uint8_t* p = &px[(y * w + x) * 4];
      p[0] = c ? 0x2b : 0xd0;
      p[1] = c ? 0x6a : 0xf0;
      p[2] = c ? 0xc0 : 0xff;
      p[3] = 0xff;
    }
  return px;
}

inline bool InitQuad(SDL_GPUDevice* dev, SDL_GPUTextureFormat fmt,
                     const char* vertSpv, const char* fragSpv, Quad& q) {
  std::vector<uint8_t> v = ReadSpirv(vertSpv), f = ReadSpirv(fragSpv);
  if (!IsSpirv(v) || !IsSpirv(f)) {
    fprintf(stderr, "render3d: bad/missing quad SPIR-V (%s / %s)\n", vertSpv,
            fragSpv);
    return false;
  }
  q.vs = MakeShader(dev, v, SDL_GPU_SHADERSTAGE_VERTEX, /*uniform_buffers=*/1);
  q.ps = MakeShader(dev, f, SDL_GPU_SHADERSTAGE_FRAGMENT, /*uniform_buffers=*/0,
                    /*samplers=*/1);
  if (!q.vs || !q.ps) {
    fprintf(stderr, "render3d: quad shader failed: %s\n", SDL_GetError());
    ReleaseQuad(dev, q);
    return false;
  }

  SDL_GPUColorTargetDescription ct{};
  ct.format = fmt;
  SDL_GPUGraphicsPipelineCreateInfo ci{};
  ci.vertex_shader = q.vs;
  ci.fragment_shader = q.ps;
  ci.vertex_input_state.vertex_buffer_descriptions = kDescB;
  ci.vertex_input_state.num_vertex_buffers = 1;
  ci.vertex_input_state.vertex_attributes = kAttrB;
  ci.vertex_input_state.num_vertex_attributes = 4;
  ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
  ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  ci.target_info.color_target_descriptions = &ct;
  ci.target_info.num_color_targets = 1;
  q.pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
  if (!q.pipe) {
    fprintf(stderr, "render3d: quad pipeline failed: %s\n", SDL_GetError());
    ReleaseQuad(dev, q);
    return false;
  }

  q.sampler = MakeSampler(dev);

  std::vector<uint8_t> px = CheckerRgba(64, 64);
  q.tex = UploadTexture2D(dev, px.data(), 64, 64);

  QuadVerts qv = MakeQuad(0.f, 0.f, 200.f, 200.f);
  q.vb = UploadBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX, &qv.v, sizeof qv.v);
  q.ib = UploadBuffer(dev, SDL_GPU_BUFFERUSAGE_INDEX, &qv.idx, sizeof qv.idx);

  if (!q.sampler || !q.tex || !q.vb || !q.ib) {
    fprintf(stderr, "render3d: quad resource failed: %s\n", SDL_GetError());
    ReleaseQuad(dev, q);
    return false;
  }
  return true;
}

inline void ReleaseQuad(SDL_GPUDevice* dev, Quad& q) {
  if (q.pipe) SDL_ReleaseGPUGraphicsPipeline(dev, q.pipe);
  if (q.vs) SDL_ReleaseGPUShader(dev, q.vs);
  if (q.ps) SDL_ReleaseGPUShader(dev, q.ps);
  if (q.sampler) SDL_ReleaseGPUSampler(dev, q.sampler);
  if (q.tex) SDL_ReleaseGPUTexture(dev, q.tex);
  if (q.vb) SDL_ReleaseGPUBuffer(dev, q.vb);
  if (q.ib) SDL_ReleaseGPUBuffer(dev, q.ib);
  q = Quad{};
}

// ---------------------------------------------------------------------------
// Iteration 6: layout-A static mesh (.obj -> VertexA, layout A pipeline).
// Adds .pcx texture upload (0064be00's RGBA path) reusing src/pcx.hpp.
// ---------------------------------------------------------------------------

struct MeshGpu {
  SDL_GPUShader* vs = nullptr;
  SDL_GPUShader* ps = nullptr;
  SDL_GPUGraphicsPipeline* pipe = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUTexture* tex = nullptr;
  SDL_GPUBuffer* vb = nullptr;
  Uint32 vertexCount = 0;
  bool Ok() const { return pipe != nullptr; }
};

inline void ReleaseMesh(SDL_GPUDevice* dev, MeshGpu& m);

// Creates the layout-A pipeline and uploads a ready-made vertex list + RGBA
// texture (fallback: checker). Shared by the .obj path and the terrain scene.
// `depthFmt` (non-INVALID) enables depth test/write for scenes that need
// occlusion (the bridge's world pass); the default keeps the old 2D-ish passes
// depthless so their render targets stay unchanged.
inline bool InitMeshVerts(SDL_GPUDevice* dev, SDL_GPUTextureFormat fmt,
                          const char* vertSpv, const char* fragSpv,
                          std::vector<VertexA> verts, std::vector<uint8_t> rgba,
                          Uint32 tw, Uint32 th, MeshGpu& m,
                          SDL_GPUTextureFormat depthFmt =
                              SDL_GPU_TEXTUREFORMAT_INVALID) {
  std::vector<uint8_t> v = ReadSpirv(vertSpv), f = ReadSpirv(fragSpv);
  if (!IsSpirv(v) || !IsSpirv(f) || verts.empty()) {
    fprintf(stderr, "render3d: bad mesh SPIR-V or empty verts (%s / %s)\n",
            vertSpv, fragSpv);
    return false;
  }

  m.vs = MakeShader(dev, v, SDL_GPU_SHADERSTAGE_VERTEX, /*uniform_buffers=*/1);
  m.ps = MakeShader(dev, f, SDL_GPU_SHADERSTAGE_FRAGMENT, /*uniform_buffers=*/0,
                    /*samplers=*/1);
  if (!m.vs || !m.ps) {
    fprintf(stderr, "render3d: mesh shader failed: %s\n", SDL_GetError());
    ReleaseMesh(dev, m);
    return false;
  }

  SDL_GPUColorTargetDescription ct{};
  ct.format = fmt;
  SDL_GPUGraphicsPipelineCreateInfo ci{};
  ci.vertex_shader = m.vs;
  ci.fragment_shader = m.ps;
  ci.vertex_input_state.vertex_buffer_descriptions = kDescA;
  ci.vertex_input_state.num_vertex_buffers = 1;
  ci.vertex_input_state.vertex_attributes = kAttrA;
  ci.vertex_input_state.num_vertex_attributes = 3;
  ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  ci.target_info.color_target_descriptions = &ct;
  ci.target_info.num_color_targets = 1;
  if (depthFmt != SDL_GPU_TEXTUREFORMAT_INVALID) {
    ci.depth_stencil_state.enable_depth_test = true;
    ci.depth_stencil_state.enable_depth_write = true;
    ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    ci.target_info.has_depth_stencil_target = true;
    ci.target_info.depth_stencil_format = depthFmt;
  }
  m.pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
  if (!m.pipe) {
    fprintf(stderr, "render3d: mesh pipeline failed: %s\n", SDL_GetError());
    ReleaseMesh(dev, m);
    return false;
  }

  m.sampler = MakeSampler(dev);
  if (rgba.empty()) {
    rgba = CheckerRgba(64, 64);
    tw = th = 64;
  }
  m.tex = UploadTexture2D(dev, rgba.data(), tw, th);
  m.vb = UploadBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX, verts.data(),
                      (Uint32)(verts.size() * sizeof(VertexA)));
  m.vertexCount = (Uint32)verts.size();
  if (!m.sampler || !m.tex || !m.vb) {
    fprintf(stderr, "render3d: mesh resource failed: %s\n", SDL_GetError());
    ReleaseMesh(dev, m);
    return false;
  }
  return true;
}

// Loads `objPath`, optionally uploads a `.pcx` texture `texPath` (fallback:
// checker), builds layout-A verts (render3d_mesh.hpp) and creates the pipeline.
inline bool InitMesh(SDL_GPUDevice* dev, SDL_GPUTextureFormat fmt,
                     const char* vertSpv, const char* fragSpv,
                     const char* objPath, const char* texPath, MeshGpu& m) {
  std::vector<uint8_t> od = ReadWholeFile(objPath);
  obj::Model model;
  try {
    model = obj::Parse(od);
  } catch (const std::exception& e) {
    fprintf(stderr, "render3d: obj parse failed (%s): %s\n", objPath, e.what());
    return false;
  }

  // Texture: decode .pcx -> RGBA. Any failure falls back to the checker.
  Uint32 tw = 0, th = 0;
  std::vector<uint8_t> rgba;
  if (texPath && *texPath)
    DecodePcxRgba(texPath, tw, th, rgba);

  std::vector<VertexA> verts = BuildMeshA(model, (float)tw, (float)th);
  if (verts.empty()) {
    fprintf(stderr, "render3d: mesh empty (%s)\n", objPath);
    return false;
  }
  return InitMeshVerts(dev, fmt, vertSpv, fragSpv, std::move(verts),
                       std::move(rgba), tw, th, m);
}

inline void ReleaseMesh(SDL_GPUDevice* dev, MeshGpu& m) {
  if (m.pipe) SDL_ReleaseGPUGraphicsPipeline(dev, m.pipe);
  if (m.vs) SDL_ReleaseGPUShader(dev, m.vs);
  if (m.ps) SDL_ReleaseGPUShader(dev, m.ps);
  if (m.sampler) SDL_ReleaseGPUSampler(dev, m.sampler);
  if (m.tex) SDL_ReleaseGPUTexture(dev, m.tex);
  if (m.vb) SDL_ReleaseGPUBuffer(dev, m.vb);
  m = MeshGpu{};
}

// ---------------------------------------------------------------------------
// Iteration 7: terrain draw path. Same layout-B shaders as the quad (clean-room
// stand-in for the terrain VS pair 0074dce8/0074eec0), TRIANGLELIST and no
// index buffer -- the 006487f0 batch is a plain vertex list flushed by 0064c8a0
// (00648f60 DrawIndexed with an identity index stream).
// ---------------------------------------------------------------------------

struct TerrainGpu {
  SDL_GPUShader* vs = nullptr;
  SDL_GPUShader* ps = nullptr;
  SDL_GPUGraphicsPipeline* pipe = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUTexture* tex = nullptr;
  SDL_GPUBuffer* vb = nullptr;
  Uint32 vertexCount = 0;
  bool Ok() const { return pipe != nullptr; }
};

inline void ReleaseTerrain(SDL_GPUDevice* dev, TerrainGpu& t);

// `verts` is a layout-B triangle list (render3d_terrain.hpp). Texture falls back
// to the checker. `vpW`/`vpH` are the viewport the pixel coords were built in.
inline bool InitTerrain(SDL_GPUDevice* dev, SDL_GPUTextureFormat fmt,
                        const char* vertSpv, const char* fragSpv,
                        std::vector<VertexB> verts, TerrainGpu& t) {
  std::vector<uint8_t> v = ReadSpirv(vertSpv), f = ReadSpirv(fragSpv);
  if (!IsSpirv(v) || !IsSpirv(f) || verts.empty()) {
    fprintf(stderr, "render3d: bad terrain SPIR-V or empty grid (%s / %s)\n",
            vertSpv, fragSpv);
    return false;
  }
  t.vs = MakeShader(dev, v, SDL_GPU_SHADERSTAGE_VERTEX, /*uniform_buffers=*/1);
  t.ps = MakeShader(dev, f, SDL_GPU_SHADERSTAGE_FRAGMENT, /*uniform_buffers=*/0,
                    /*samplers=*/1);
  if (!t.vs || !t.ps) {
    fprintf(stderr, "render3d: terrain shader failed: %s\n", SDL_GetError());
    ReleaseTerrain(dev, t);
    return false;
  }

  SDL_GPUColorTargetDescription ct{};
  ct.format = fmt;
  SDL_GPUGraphicsPipelineCreateInfo ci{};
  ci.vertex_shader = t.vs;
  ci.fragment_shader = t.ps;
  ci.vertex_input_state.vertex_buffer_descriptions = kDescB;
  ci.vertex_input_state.num_vertex_buffers = 1;
  ci.vertex_input_state.vertex_attributes = kAttrB;
  ci.vertex_input_state.num_vertex_attributes = 4;
  ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  ci.target_info.color_target_descriptions = &ct;
  ci.target_info.num_color_targets = 1;
  t.pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
  if (!t.pipe) {
    fprintf(stderr, "render3d: terrain pipeline failed: %s\n", SDL_GetError());
    ReleaseTerrain(dev, t);
    return false;
  }

  t.sampler = MakeSampler(dev);
  std::vector<uint8_t> px = CheckerRgba(64, 64);
  t.tex = UploadTexture2D(dev, px.data(), 64, 64);
  t.vb = UploadBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX, verts.data(),
                      (Uint32)(verts.size() * sizeof(VertexB)));
  t.vertexCount = (Uint32)verts.size();
  if (!t.sampler || !t.tex || !t.vb) {
    fprintf(stderr, "render3d: terrain resource failed: %s\n", SDL_GetError());
    ReleaseTerrain(dev, t);
    return false;
  }
  return true;
}

inline void ReleaseTerrain(SDL_GPUDevice* dev, TerrainGpu& t) {
  if (t.pipe) SDL_ReleaseGPUGraphicsPipeline(dev, t.pipe);
  if (t.vs) SDL_ReleaseGPUShader(dev, t.vs);
  if (t.ps) SDL_ReleaseGPUShader(dev, t.ps);
  if (t.sampler) SDL_ReleaseGPUSampler(dev, t.sampler);
  if (t.tex) SDL_ReleaseGPUTexture(dev, t.tex);
  if (t.vb) SDL_ReleaseGPUBuffer(dev, t.vb);
  t = TerrainGpu{};
}

}  // namespace render3d
