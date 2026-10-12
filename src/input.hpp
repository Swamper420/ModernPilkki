// Keyboard bitmap (WndProc 005f3820 + FUN_0049e3c0).
// WM_KEYDOWN/UP wParam -> game key id -> bit in u32 bitmap:
// 0 VK_UP, 1 VK_DOWN, 2 VK_LEFT, 3 VK_RIGHT, 4 VK_PRIOR, 5 VK_NEXT,
// 6 VK_RETURN, 7 VK_ESCAPE, 8 M (mute toggle, port-local).
// Other keys (F1-F10, End/Home/Ins/Del, PgUp/Dn+arrows) route to the
// FUN_0049e270 event queue, Shift/Ctrl/Alt set flags 7c6912/13/14 —
// not ported yet (open).
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace input {

struct Keys {
  uint32_t bits = 0;
};

// FUN_0049e3c0 equiv: id & 0x1f, set/clear bit.
inline void Set(Keys &k, uint32_t id, bool down) {
  if (down)
    k.bits |= (uint32_t)1 << (id & 0x1f);
  else
    k.bits &= ~((uint32_t)1 << (id & 0x1f));
}

inline bool Down(const Keys &k, uint32_t id) {
  return (k.bits & ((uint32_t)1 << (id & 0x1f))) != 0;
}

// SDL keycode -> game key id, -1 when unmapped.
// ponytail: KP_ENTER shares id 6 (laptops); game saw one VK_RETURN.
inline int SdlToId(SDL_Keycode kc) {
  switch (kc) {
    case SDLK_UP: return 0;
    case SDLK_DOWN: return 1;
    case SDLK_LEFT: return 2;
    case SDLK_RIGHT: return 3;
    case SDLK_PAGEUP: return 4;
    case SDLK_PAGEDOWN: return 5;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return 6;
    case SDLK_ESCAPE: return 7;
    case SDLK_M: return 8;
    default: return -1;
  }
}

}  // namespace input
