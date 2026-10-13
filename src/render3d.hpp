// Iteration 1 of the SDL_GPU port of the PLAY-screen 3D scene.
// Device + swapchain skeleton only: no shaders, no scene geometry, no pipelines.
// Pure math (Letterbox/ClearGrey) mirrors the original:
//   Letterbox  <- 0064ff60 (docs/render.md 4.4)
//   ClearGrey  <- clear color in 0064ebb0 (docs/render.md 12.3, "0.75 grey")
#pragma once

#include <SDL3/SDL.h>

namespace render3d {

// Pure, unit-testable; no GPU state.
struct Rect {
  float x, y, w, h;
};

// Aspect-preserving, centered viewport inside a winW x winH target.
// Degenerate inputs collapse to an empty rect.
inline Rect Letterbox(int winW, int winH, float aspect) {
  if (winW <= 0 || winH <= 0 || aspect <= 0.f) return {0.f, 0.f, 0.f, 0.f};
  float w = (float)winW, h = (float)winH;
  if (w / h > aspect)
    w = h * aspect;  // window wider than target -> pillarbox
  else
    h = w / aspect;  // window taller than target -> letterbox
  return {(float)winW - w > 0 ? ((float)winW - w) * 0.5f : 0.f,
          (float)winH - h > 0 ? ((float)winH - h) * 0.5f : 0.f, w, h};
}

// Original back-buffer clear factor (0.75 grey).
inline float ClearGrey() { return 0.75f; }

struct Ctx {
  SDL_GPUDevice *dev = nullptr;
  SDL_Window *win = nullptr;
  int pixW = 0, pixH = 0;

  SDL_GPUCommandBuffer *cmd = nullptr;
  SDL_GPUTexture *swap = nullptr;

  // false (no crash) when no GPU driver/device is available (headless/dummy).
  bool Init(SDL_Window *w) {
    win = w;
    dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!dev) return false;
    if (!SDL_ClaimWindowForGPUDevice(dev, win)) {
      SDL_DestroyGPUDevice(dev);
      dev = nullptr;
      return false;
    }
    SDL_GetWindowSizeInPixels(win, &pixW, &pixH);
    return true;
  }

  void Shutdown() {
    if (dev) {
      SDL_WaitForGPUIdle(dev);
      if (win) SDL_ReleaseWindowFromGPUDevice(dev, win);
      SDL_DestroyGPUDevice(dev);
    }
    dev = nullptr;
    win = nullptr;
    cmd = nullptr;
    swap = nullptr;
  }

  // Alias kept for callers that want an explicit "close this frame/context".
  void Close() { Shutdown(); }

  void Resize(int w, int h) {
    pixW = w > 0 ? w : pixW;
    pixH = h > 0 ? h : pixH;
  }

  // Acquire a command buffer + swapchain texture. false when not ready
  // (no device, acquire failed, or window minimized -> null swapchain).
  bool BeginFrame() {
    if (!dev) return false;
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return false;
    Uint32 sw = 0, sh = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &swap, &sw, &sh)) {
      SDL_CancelGPUCommandBuffer(cmd);
      cmd = nullptr;
      return false;
    }
    if (!swap) {
      SDL_CancelGPUCommandBuffer(cmd);
      cmd = nullptr;
      return false;
    }
    pixW = (int)sw;
    pixH = (int)sh;
    return true;
  }

  // Clear + letterbox viewport, then submit. No geometry yet.
  void EndFrame() {
    if (!cmd) return;
    float g = ClearGrey();
    SDL_GPUColorTargetInfo ct{};
    ct.texture = swap;
    ct.clear_color = {g, g, g, 1.f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    Rect r = Letterbox(pixW, pixH, 4.f / 3.f);
    SDL_GPUViewport vp{r.x, r.y, r.w, r.h, 0.f, 1.f};
    SDL_SetGPUViewport(rp, &vp);
    SDL_EndGPURenderPass(rp);
    SDL_SubmitGPUCommandBuffer(cmd);
    cmd = nullptr;
    swap = nullptr;
  }
};

}  // namespace render3d
