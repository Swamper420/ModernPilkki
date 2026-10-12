#include <SDL3/SDL.h>

int main(int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_Window *w =
      SDL_CreateWindow("ModernPilkki", 800, 600, SDL_WINDOW_RESIZABLE);
  if (!w) {
    SDL_Quit();
    return 1;
  }
  bool run = true;
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT) run = false;
    SDL_Delay(16);
  }
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
