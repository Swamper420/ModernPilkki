// Smallest assert: menu draws over art/gradient with synthetic
// lakes no crash, selection wrap works, setup row clamps, click picks.
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "menu.hpp"

int main() {
  ui::Game g;
  ui::Init(g, {{"a.ves", "A", "North"}, {"b.ves", "B", ""}});
  menu::Setup s;
  assert(g.state == ui::State::Title);

  // Keyboard: Title -> LakeSelect, wrap both ends (via MenuPress).
  menu::MenuPress(g, s, 6);
  assert(g.state == ui::State::LakeSelect);
  menu::MenuPress(g, s, 0);
  assert(g.sel == 1);
  menu::MenuPress(g, s, 1);
  assert(g.sel == 0);
  menu::MenuPress(g, s, 6);
  assert(g.needLoad && g.pendIdx == 0);

  // Setup row: time cycles 600->900->600, bots PgUp/PgDn clamp 0..8.
  assert(s.timeSec == 600 && s.bots == 5);
  menu::MenuPress(g, s, 3);
  assert(s.timeSec == 900);
  menu::MenuPress(g, s, 2);
  assert(s.timeSec == 600);
  menu::MenuPress(g, s, 5);
  assert(s.bots == 6);
  menu::MenuPress(g, s, 4);
  assert(s.bots == 5);
  s.bots = 8;
  menu::MenuPress(g, s, 5);
  assert(s.bots == 8);
  s.bots = 0;
  menu::MenuPress(g, s, 4);
  assert(s.bots == 0);
  s.bots = 5;

  // Click: second lake row picks it (list top row 3, rowH 12).
  ui::Game c;
  ui::Init(c, {{"a.ves", "A", "North"}, {"b.ves", "B", ""}});
  menu::MenuPress(c, s, 6);
  assert(menu::Click(c, s, 20, menu::kListTop * 12 + 1 * 12 + 6, 800, 600));
  assert(c.sel == 1 && c.needLoad && c.pendIdx == 1);
  // Click: setup row halves adjust time / bots.
  int t0 = s.timeSec, b0 = s.bots;
  assert(menu::Click(c, s, 100, 600 - 48, 800, 600) && s.timeSec != t0);
  assert(menu::Click(c, s, 700, 600 - 48, 800, 600) && s.bots != b0);
  // Click: Title advances like Enter.
  ui::Game t;
  ui::Init(t, {{"a.ves", "A", ""}});
  assert(menu::Click(t, s, 400, 300, 800, 600));
  assert(t.state == ui::State::LakeSelect);

  // Draw both menu states headless: art or gradient, never crash.
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("menu tests pass (sdl unavailable, logic ok)\n");
    return 0;
  }
  SDL_Window *w = SDL_CreateWindow("t", 800, 600, 0);
  assert(w);
  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  assert(r);
  menu::Back b;
  menu::Draw(r, b, t, s, 800, 600);  // LakeSelect
  ui::Game tt;
  ui::Init(tt, {});
  menu::Draw(r, b, tt, s, 800, 600);  // Title, no lakes
  menu::Draw(nullptr, b, tt, s, 800, 600);  // null renderer safe
  SDL_RenderPresent(r);
  menu::FreeBack(b);
  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  printf("menu tests pass\n");
  return 0;
}
