#include <SDL3/SDL.h>

#include "input.hpp"

int main(int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_Window *w =
      SDL_CreateWindow("ModernPilkki", 800, 600, SDL_WINDOW_RESIZABLE);
  if (!w) {
    SDL_Quit();
    return 1;
  }
  input::Keys keys;
  bool run = true;
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) run = false;
      if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        int id = input::SdlToId(e.key.key);
        if (id >= 0)
          input::Set(keys, (uint32_t)id, e.type == SDL_EVENT_KEY_DOWN);
      }
    }
    SDL_Delay(16);
  }
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
