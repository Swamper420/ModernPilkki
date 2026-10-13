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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace render3d {

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
                                 SDL_GPUShaderStage stage) {
  SDL_GPUShaderCreateInfo ci{};
  ci.code_size = code.size();
  ci.code = code.data();
  ci.entrypoint = "main";
  ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
  ci.stage = stage;
  ci.num_samplers = 0;
  ci.num_storage_textures = 0;
  ci.num_storage_buffers = 0;
  ci.num_uniform_buffers = 0;
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

}  // namespace render3d
