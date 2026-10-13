// Iteration 9 of the SDL_GPU port: bridge the 3D lake scene into the existing
// SDL_Renderer game loop. Owns an offscreen SDL_GPU device that renders the
// world to an RGBA8 texture, downloads it each frame and uploads it into an
// SDL_Texture the SDL_Renderer blits as the PLAY background -- so the HUD, menus
// and overlays (all SDL_Renderer) stay untouched.
//
// The GPU device never claims the window, so it coexists with the renderer.
// ponytail: full-frame GPU->CPU readback every frame; replace with a real
// SDL_GPU present path (and move the HUD to SDL_GPU) when the 3D scene is the
// primary renderer, not a background.
#pragma once

#include <SDL3/SDL.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "objects.hpp"
#include "render3d_objects.hpp"
#include "render3d_pipeline.hpp"
#include "render3d_scene.hpp"
#include "ves.hpp"

namespace render3d {

struct Bridge {
  SDL_GPUDevice* dev = nullptr;
  SDL_Renderer* ren = nullptr;
  MeshGpu mesh;
  std::vector<MeshGpu> props;  // it10: lake objects, baked per source file
  SDL_GPUTexture* target = nullptr;
  SDL_GPUTexture* depth = nullptr;  // it10 depth-stencil for the world pass
  SDL_GPUTransferBuffer* tb = nullptr;
  SDL_Texture* tex = nullptr;  // renderer-side, updated from the download
  int w = 0, h = 0;
  std::vector<uint8_t> pix;
  SceneCam cam;  // last camera, for the HUD/overlay to follow

  bool Ok() const { return tex != nullptr; }

  void Close() {
    for (auto& p : props)
      if (p.Ok()) ReleaseMesh(dev, p);
    props.clear();
    if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb);
    if (target) SDL_ReleaseGPUTexture(dev, target);
    if (depth) SDL_ReleaseGPUTexture(dev, depth);
    if (mesh.Ok()) ReleaseMesh(dev, mesh);
    if (tex) SDL_DestroyTexture(tex);
    if (dev) SDL_DestroyGPUDevice(dev);
    tb = nullptr;
    target = nullptr;
    depth = nullptr;
    tex = nullptr;
    dev = nullptr;
    ren = nullptr;
  }

  // `lakePath` supplies the DEPTHMAP grid; shaders are the layout-A mesh pair.
  bool Init(SDL_Renderer* r, const std::string& lakePath, const char* vertSpv,
            const char* fragSpv, int width, int height) {
    Close();
    ren = r;
    dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!dev) return false;

    ves::Grid grid;
    try {
      for (auto& s : ves::Parse(ReadWholeFile(lakePath.c_str())))
        if (s.tag == "DEPTHMAP") grid = ves::ParseGrid(s.payload);
    } catch (const std::exception&) {
    }
    if (!InitMeshVerts(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, vertSpv,
                       fragSpv, BuildTerrainMesh(grid), BuildTerrainTex(grid),
                       TexWidthFor(grid.w), grid.h, mesh, kDepthFmt))
      return false;

    w = width;
    h = height;
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tci.width = (Uint32)w;
    tci.height = (Uint32)h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    target = SDL_CreateGPUTexture(dev, &tci);
    SDL_GPUTextureCreateInfo dci = tci;
    dci.format = kDepthFmt;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth = SDL_CreateGPUTexture(dev, &dci);
    SDL_GPUTransferBufferCreateInfo xbci{};
    xbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    xbci.size = (Uint32)(w * h * 4);
    tb = SDL_CreateGPUTransferBuffer(dev, &xbci);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING, w, h);
    pix.resize((size_t)w * h * 4);
    return tex != nullptr && target != nullptr && tb != nullptr &&
           depth != nullptr;
  }

  // it10: bake the lake's static objpos objects into world-space meshes.
  // `root` is the extracted asset root; `tab` the objects.dat class table for
  // the active season. Safe no-op (returns false) on missing assets.
  bool InitProps(const std::string& root, const std::vector<ves::ObjGroup>& groups,
                 const objects::ObjTable* tab, const ves::Grid& depth,
                 float zScale, const char* vertSpv, const char* fragSpv) {
    if (!dev) return false;
    std::map<std::string, std::vector<PropInstance>> byFile;
    for (auto& p : PropInstances(groups, tab, depth)) byFile[p.file].push_back(p);
    for (auto& kv : byFile) {
      auto od = ReadWholeFile((root + "/" + NormPath(kv.first)).c_str());
      if (od.empty()) continue;
      obj::Model mo;
      try {
        mo = obj::Parse(od);
      } catch (const std::exception&) {
        continue;
      }
      Uint32 tw = 0, th = 0;
      std::vector<uint8_t> rgba;
      if (!mo.tex.empty())
        DecodePcxRgba((root + "/" + NormPath(mo.tex[0])).c_str(), tw, th, rgba);
      auto verts = BakeProps(mo, kv.second, depth, zScale, (float)tw, (float)th);
      MeshGpu m;
      if (InitMeshVerts(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, vertSpv,
                        fragSpv, std::move(verts), std::move(rgba), tw, th, m,
                        kDepthFmt))
        props.push_back(m);
    }
    return !props.empty();
  }

  // Renders one frame from `c` and refreshes `tex`. Safe no-op when !Ok().
  void RenderFrame(const SceneCam& c) {
    cam = c;
    if (!Ok() || !target || !tb) return;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return;
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.clear_color = {0.75f, 0.75f, 0.75f, 1.f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = depth;
    dt.clear_depth = 1.f;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* rp =
        SDL_BeginGPURenderPass(cmd, &ct, 1, depth ? &dt : nullptr);
    SDL_GPUViewport vp{0, 0, (float)w, (float)h, 0, 1};
    SDL_SetGPUViewport(rp, &vp);
    Mat4 mvp = ViewProj(c, h > 0 ? (float)w / (float)h : 1.f);
    SDL_PushGPUVertexUniformData(cmd, 0, mvp.m, sizeof mvp.m);
    SDL_BindGPUGraphicsPipeline(rp, mesh.pipe);
    SDL_GPUBufferBinding vb{mesh.vb, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUTextureSamplerBinding ts{mesh.tex, mesh.sampler};
    SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
    SDL_DrawGPUPrimitives(rp, mesh.vertexCount, 1, 0, 0);
    for (auto& p : props) {  // it10: same MVP, baked world-space objects
      SDL_BindGPUGraphicsPipeline(rp, p.pipe);
      SDL_GPUBufferBinding pb{p.vb, 0};
      SDL_BindGPUVertexBuffers(rp, 0, &pb, 1);
      SDL_GPUTextureSamplerBinding pts{p.tex, p.sampler};
      SDL_BindGPUFragmentSamplers(rp, 0, &pts, 1);
      SDL_DrawGPUPrimitives(rp, p.vertexCount, 1, 0, 0);
    }
    SDL_EndGPURenderPass(rp);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{target, 0, 0, 0, 0, 0, (Uint32)w, (Uint32)h, 1};
    SDL_GPUTextureTransferInfo dst{tb, 0, (Uint32)w, (Uint32)h};
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!f) return;
    SDL_WaitForGPUFences(dev, true, &f, 1);
    SDL_ReleaseGPUFence(dev, f);
    if (const uint8_t* p =
            (const uint8_t*)SDL_MapGPUTransferBuffer(dev, tb, false)) {
      std::memcpy(pix.data(), p, pix.size());
      SDL_UnmapGPUTransferBuffer(dev, tb);
      SDL_UpdateTexture(tex, nullptr, pix.data(), w * 4);
    }
  }
};

}  // namespace render3d
